#include <nano/lib/json_writer.hpp>

#include <boost/json/serialize.hpp>

namespace nano::json
{
void object_writer::add_child (std::string const & key, object_writer const & child)
{
	obj[key] = child.obj;
}

void object_writer::put_child (std::string const & key, object_writer const & child)
{
	add_child (key, child);
}

void object_writer::add_child (std::string const & key, array_writer const & child)
{
	obj[key] = child.value ();
}

void object_writer::put_child (std::string const & key, array_writer const & child)
{
	add_child (key, child);
}

void object_writer::add_child (std::string const & key, boost::property_tree::ptree const & child)
{
	obj[key] = from_ptree (child);
}

void object_writer::put_child (std::string const & key, boost::property_tree::ptree const & child)
{
	add_child (key, child);
}

void object_writer::replace (boost::property_tree::ptree const & tree)
{
	auto value = from_ptree (tree);
	if (auto * source = value.if_object ())
	{
		obj = *source;
	}
	else
	{
		obj.clear ();
	}
}

bool object_writer::empty () const
{
	return obj.empty ();
}

boost::json::object const & object_writer::value () const
{
	return obj;
}

boost::json::object & object_writer::value ()
{
	return obj;
}

std::string object_writer::serialize () const
{
	return boost::json::serialize (obj);
}

void array_writer::push (object_writer const & child)
{
	arr.push_back (child.value ());
}

void array_writer::push (array_writer const & child)
{
	arr.push_back (child.arr);
}

void array_writer::push (boost::property_tree::ptree const & child)
{
	arr.push_back (from_ptree (child));
}

std::size_t array_writer::size () const
{
	return arr.size ();
}

bool array_writer::empty () const
{
	return arr.empty ();
}

boost::json::array const & array_writer::value () const
{
	return arr;
}

boost::json::array & array_writer::value ()
{
	return arr;
}

std::string array_writer::serialize () const
{
	return boost::json::serialize (arr);
}

boost::json::value from_ptree (boost::property_tree::ptree const & tree)
{
	// A leaf carries no children; its data() is the (already-stringified) scalar.
	// write_json ignores data() when a node has children, so we do the same.
	if (tree.empty ())
	{
		return boost::json::string (tree.data ());
	}

	// property_tree encodes JSON arrays as children that all have empty keys.
	bool const is_array = std::all_of (tree.begin (), tree.end (), [] (auto const & entry) {
		return entry.first.empty ();
	});

	if (is_array)
	{
		boost::json::array arr;
		arr.reserve (tree.size ());
		for (auto const & entry : tree)
		{
			arr.push_back (from_ptree (entry.second));
		}
		return arr;
	}

	boost::json::object obj;
	for (auto const & entry : tree)
	{
		obj[entry.first] = from_ptree (entry.second);
	}
	return obj;
}
}
