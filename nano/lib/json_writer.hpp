#pragma once

#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <type_traits>

namespace nano::json
{
class array_writer;

/**
 * Adapter that mimics the subset of the boost::property_tree::ptree API used by the
 * RPC handlers, but is backed by boost::json for dramatically faster serialization.
 *
 * Backward-compatibility contract: boost::property_tree::write_json emits *every*
 * scalar leaf as a quoted JSON string regardless of its C++ type (e.g. put("count", 5)
 * produces "count":"5", put("confirmed", true) produces "confirmed":"true"). External
 * consumers and the rpc_test harness parse responses back with property_tree and read
 * the values as quoted strings. To preserve this, scalars are stringified via
 * property_tree's own translator (a throwaway ptree::put_value) and stored as JSON
 * strings -- never as native JSON numbers/booleans.
 */
class object_writer
{
public:
	object_writer () = default;

	// Scalar leaf. String-like values are stored verbatim; everything else is
	// stringified via property_tree's translator so the bytes match write_json exactly.
	template <typename T>
	void put (std::string const & key, T const & value)
	{
		obj[key] = to_compat_string (value);
	}

	// Nested object. property_tree exposes both add_child and put_child; we keep both.
	void add_child (std::string const & key, object_writer const & child);
	void put_child (std::string const & key, object_writer const & child);

	// Nested array (the property_tree "children with empty keys" idiom maps to a real array).
	void add_child (std::string const & key, array_writer const & child);
	void put_child (std::string const & key, array_writer const & child);

	bool empty () const;

	boost::json::object const & value () const;
	boost::json::object & value ();

	// Compact serialization via boost::json. Differs from write_json only in
	// insignificant whitespace and the empty-container representation ({} vs ""),
	// neither of which affects re-parsing.
	std::string serialize () const;

	// Stringify a scalar exactly as property_tree's write_json would, so output is
	// identical when re-parsed. Shared by object_writer::put and array_writer::push.
	template <typename T>
	static boost::json::value to_compat_string (T const & value)
	{
		if constexpr (std::is_convertible_v<T, std::string_view>)
		{
			return boost::json::string (std::string_view (value));
		}
		else
		{
			// Reuse property_tree's translator: a throwaway node stringifies the value
			// via the exact code path ptree::put would have used (boolalpha for bool,
			// max_digits10 precision for floating point, etc.).
			boost::property_tree::ptree tmp;
			tmp.put_value (value);
			return boost::json::string (tmp.data ());
		}
	}

private:
	boost::json::object obj;
};

/**
 * Backs the property_tree array idiom (children pushed with empty keys) with a real
 * boost::json::array. Scalars pushed here follow the same stringification contract.
 */
class array_writer
{
public:
	array_writer () = default;

	template <typename T>
	void push (T const & value)
	{
		arr.push_back (object_writer::to_compat_string (value));
	}

	void push (object_writer const & child);
	void push (array_writer const & child);

	// Sort the underlying array. Comparator receives boost::json::value const &.
	template <typename Compare>
	void sort (Compare comp)
	{
		std::sort (arr.begin (), arr.end (), comp);
	}

	bool empty () const;

	boost::json::array const & value () const;
	boost::json::array & value ();

	std::string serialize () const;

private:
	boost::json::array arr;

	friend class object_writer;
};

/**
 * Bridge for "foreign" property_trees produced by other subsystems (block
 * serialize_json, bootstrap info, telemetry, container_info) that are embedded into a
 * response. Recursively converts to a boost::json::value preserving the quoting
 * contract: leaf data() is copied verbatim as a JSON string, all-empty-key children
 * become an array, named children become an object.
 */
boost::json::value from_ptree (boost::property_tree::ptree const & tree);
}
