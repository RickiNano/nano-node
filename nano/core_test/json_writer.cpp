#include <nano/lib/json_writer.hpp>
#include <nano/lib/numbers.hpp>

#include <gtest/gtest.h>

#include <boost/json/serialize.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <cstdint>
#include <limits>
#include <sstream>
#include <string>

namespace
{
// Serialize a ptree the way the RPC layer does today.
std::string write_ptree (boost::property_tree::ptree const & tree)
{
	std::ostringstream os;
	boost::property_tree::write_json (os, tree);
	return os.str ();
}

// Parse a JSON string back into a ptree (the way external consumers and rpc_test do).
boost::property_tree::ptree reparse (std::string const & json)
{
	std::istringstream is{ json };
	boost::property_tree::ptree tree;
	boost::property_tree::read_json (is, tree);
	return tree;
}

// The backward-compat gate: a ptree built/serialized the old way must re-parse to the
// same tree as the equivalent object_writer output. This ignores irrelevant whitespace
// and the {} vs "" empty-container representation while enforcing the real contract:
// every scalar leaf is the identical quoted string and the structure matches.
void assert_equivalent (boost::property_tree::ptree const & expected, std::string const & actual_json)
{
	ASSERT_EQ (reparse (write_ptree (expected)), reparse (actual_json));
}

// Tightest possible check on the stringification itself: object_writer must store the
// exact same string property_tree's put_value would have stored.
template <typename T>
void assert_scalar (T const & value)
{
	boost::property_tree::ptree pt;
	pt.put ("v", value);

	nano::json::object_writer writer;
	writer.put ("v", value);

	auto const & stored = writer.value ().at ("v");
	ASSERT_TRUE (stored.is_string ());
	ASSERT_EQ (std::string (stored.as_string ().c_str ()), pt.get<std::string> ("v"));

	assert_equivalent (pt, writer.serialize ());
}
}

TEST (json_writer, scalar_string)
{
	assert_scalar (std::string{ "hello" });
	assert_scalar (std::string{ "" });
	assert_scalar ("c-string");
}

TEST (json_writer, scalar_bool)
{
	// property_tree uses boolalpha -> "true"/"false", not "1"/"0".
	assert_scalar (true);
	assert_scalar (false);

	nano::json::object_writer writer;
	writer.put ("v", true);
	ASSERT_EQ (std::string (writer.value ().at ("v").as_string ().c_str ()), "true");
}

TEST (json_writer, scalar_integer)
{
	assert_scalar (0);
	assert_scalar (5);
	assert_scalar (-17);
	assert_scalar (static_cast<uint64_t> (18446744073709551615ull));
}

TEST (json_writer, scalar_double)
{
	// property_tree formats floating point at max_digits10 precision, not the default
	// ostream precision of 6 -- so reusing its translator is mandatory.
	assert_scalar (1.0);
	assert_scalar (1.5);
	assert_scalar (1.0 / 3.0);
	assert_scalar (0.000001);
	assert_scalar (1234567.891011);
}

TEST (json_writer, scalar_uint128)
{
	assert_scalar (nano::uint128_t{ 0 });
	assert_scalar (nano::uint128_t{ 1 });
	assert_scalar (std::numeric_limits<nano::uint128_t>::max ());
}

TEST (json_writer, nested_object)
{
	boost::property_tree::ptree pt;
	pt.put ("name", "outer");
	boost::property_tree::ptree child;
	child.put ("count", 3);
	child.put ("flag", true);
	pt.add_child ("inner", child);

	nano::json::object_writer writer;
	writer.put ("name", "outer");
	nano::json::object_writer inner;
	inner.put ("count", 3);
	inner.put ("flag", true);
	writer.add_child ("inner", inner);

	assert_equivalent (pt, writer.serialize ());
}

TEST (json_writer, array_of_scalars)
{
	boost::property_tree::ptree arr;
	for (auto const & v : { "a", "b", "c" })
	{
		boost::property_tree::ptree entry;
		entry.put ("", v);
		arr.push_back (std::make_pair ("", entry));
	}
	boost::property_tree::ptree pt;
	pt.add_child ("list", arr);

	nano::json::array_writer aw;
	aw.push ("a");
	aw.push ("b");
	aw.push ("c");
	nano::json::object_writer writer;
	writer.add_child ("list", aw);

	assert_equivalent (pt, writer.serialize ());
}

TEST (json_writer, array_of_objects)
{
	boost::property_tree::ptree arr;
	for (int i = 0; i < 3; ++i)
	{
		boost::property_tree::ptree entry;
		entry.put ("id", i);
		arr.push_back (std::make_pair ("", entry));
	}
	boost::property_tree::ptree pt;
	pt.add_child ("items", arr);

	nano::json::array_writer aw;
	for (int i = 0; i < 3; ++i)
	{
		nano::json::object_writer entry;
		entry.put ("id", i);
		aw.push (entry);
	}
	nano::json::object_writer writer;
	writer.add_child ("items", aw);

	assert_equivalent (pt, writer.serialize ());
}

TEST (json_writer, empty_child_matches_property_tree)
{
	// property_tree serializes an empty child node as "" (an empty string), whereas
	// object_writer emits {}. Both re-parse to an empty node, which is the contract.
	boost::property_tree::ptree pt;
	boost::property_tree::ptree empty_child;
	pt.add_child ("blocks", empty_child);

	nano::json::object_writer writer;
	nano::json::object_writer empty_writer;
	writer.add_child ("blocks", empty_writer);

	assert_equivalent (pt, writer.serialize ());
}

TEST (json_writer, empty_object_is_empty)
{
	nano::json::object_writer writer;
	ASSERT_TRUE (writer.empty ());
	writer.put ("v", 1);
	ASSERT_FALSE (writer.empty ());
}

TEST (json_writer, array_sort_by_amount)
{
	// Mirrors accounts_receivable: sort entries by their "amount" descending.
	auto build_entry = [] (auto & container, nano::uint128_t amount) {
		nano::json::object_writer e;
		e.put ("amount", amount.convert_to<std::string> ());
		container.push (e);
	};

	nano::json::array_writer aw;
	build_entry (aw, nano::uint128_t{ 5 });
	build_entry (aw, nano::uint128_t{ 100 });
	build_entry (aw, nano::uint128_t{ 50 });

	aw.sort ([] (boost::json::value const & a, boost::json::value const & b) {
		auto parse = [] (boost::json::value const & v) {
			return nano::uint128_t{ std::string (v.as_object ().at ("amount").as_string ().c_str ()) };
		};
		return parse (a) > parse (b);
	});

	ASSERT_EQ (std::string (aw.value ().at (0).as_object ().at ("amount").as_string ().c_str ()), "100");
	ASSERT_EQ (std::string (aw.value ().at (1).as_object ().at ("amount").as_string ().c_str ()), "50");
	ASSERT_EQ (std::string (aw.value ().at (2).as_object ().at ("amount").as_string ().c_str ()), "5");
}

TEST (json_writer, from_ptree_foreign)
{
	// Simulates an embedded foreign ptree (e.g. block->serialize_json): a mix of an
	// object with scalar leaves and a nested array of objects.
	boost::property_tree::ptree block;
	block.put ("type", "state");
	block.put ("balance", "1000");

	boost::property_tree::ptree links;
	for (auto const & hash : { "0000000000000000000000000000000000000000000000000000000000000001", "00000000000000000000000000000000000000000000000000000000000000FF" })
	{
		boost::property_tree::ptree entry;
		entry.put ("hash", hash);
		links.push_back (std::make_pair ("", entry));
	}
	block.add_child ("links", links);

	auto converted = nano::json::from_ptree (block);
	assert_equivalent (block, boost::json::serialize (converted));
}

TEST (json_writer, from_ptree_scalar_leaf)
{
	boost::property_tree::ptree leaf;
	leaf.put_value ("solo");
	auto converted = nano::json::from_ptree (leaf);
	ASSERT_TRUE (converted.is_string ());
	ASSERT_EQ (std::string (converted.as_string ().c_str ()), "solo");
}

// The handlers embed local/foreign ptrees directly via response_l.add_child(key, ptree).
TEST (json_writer, add_child_ptree_bridge)
{
	boost::property_tree::ptree child;
	child.put ("count", 7);
	child.put ("flag", false);
	boost::property_tree::ptree pt;
	pt.add_child ("inner", child);

	nano::json::object_writer writer;
	writer.add_child ("inner", child);

	assert_equivalent (pt, writer.serialize ());
}

TEST (json_writer, put_child_ptree_bridge)
{
	// Mirrors response_l.put_child("metrics", metrics) in telemetry().
	boost::property_tree::ptree metrics;
	for (int i = 0; i < 2; ++i)
	{
		boost::property_tree::ptree entry;
		entry.put ("address", "::1");
		entry.put ("port", 7075 + i);
		metrics.push_back (std::make_pair ("", entry));
	}
	boost::property_tree::ptree pt;
	pt.put_child ("metrics", metrics);

	nano::json::object_writer writer;
	writer.put_child ("metrics", metrics);

	assert_equivalent (pt, writer.serialize ());
}

// stats()/telemetry() let external code fill a ptree, then replace response_l with it.
TEST (json_writer, replace_from_ptree)
{
	boost::property_tree::ptree tree;
	tree.put ("blocks", 100);
	tree.put ("cemented", 90);
	boost::property_tree::ptree nested;
	nested.put ("x", 1);
	tree.add_child ("nested", nested);

	nano::json::object_writer writer;
	writer.put ("stale", "should be cleared");
	writer.replace (tree);

	assert_equivalent (tree, writer.serialize ());
	ASSERT_FALSE (writer.empty ());
	ASSERT_FALSE (writer.value ().contains ("stale"));
}

TEST (json_writer, replace_empty_clears)
{
	// An empty source must leave response_l empty, so response_errors() reports
	// empty_response exactly as the property_tree path did.
	boost::property_tree::ptree empty_tree;
	nano::json::object_writer writer;
	writer.put ("stale", "x");
	writer.replace (empty_tree);
	ASSERT_TRUE (writer.empty ());
}
