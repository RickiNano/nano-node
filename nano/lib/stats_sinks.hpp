#pragma once

#include <nano/lib/stats.hpp>

#include <boost/format.hpp>
#include <boost/json.hpp>

namespace nano
{
/** JSON sink. The resulting JSON object is provided as both a json::object (to_object) and a string (to_string) */
class stat_json_writer : public nano::stat_log_sink
{
	boost::json::object tree;
	boost::json::array entries;

public:
	std::ostream & out () override
	{
		return sstr;
	}

	void begin () override
	{
		tree.clear ();
		entries.clear ();
	}

	void write_header (std::string const & header, std::chrono::system_clock::time_point & walltime) override
	{
		std::time_t now = std::chrono::system_clock::to_time_t (walltime);
		tm tm = *localtime (&now);
		tree["type"] = header;
		tree["created"] = tm_to_string (tm);
	}

	void write_counter_entry (tm & tm, std::string const & type, std::string const & detail, std::string const & dir, uint64_t value) override
	{
		boost::json::object entry;
		entry["time"] = (boost::format ("%02d:%02d:%02d") % tm.tm_hour % tm.tm_min % tm.tm_sec).str ();
		entry["type"] = type;
		entry["detail"] = detail;
		entry["dir"] = dir;
		entry["value"] = value;
		entries.push_back (std::move (entry));
	}

	void write_sampler_entry (tm & tm, const std::string & sample, const std::vector<stats::sampler_value_t> & values, std::pair<stats::sampler_value_t, stats::sampler_value_t> expected_min_max) override
	{
		boost::json::object entry;
		entry["time"] = (boost::format ("%02d:%02d:%02d") % tm.tm_hour % tm.tm_min % tm.tm_sec).str ();
		entry["sample"] = sample;
		entry["min"] = expected_min_max.first;
		entry["max"] = expected_min_max.second;
		boost::json::array values_array;
		for (const auto & value : values)
		{
			values_array.push_back (value);
		}
		entry["values"] = std::move (values_array);
		entries.push_back (std::move (entry));
	}

	void finalize () override
	{
		tree["entries"] = std::move (entries);
	}

	std::string to_string () override
	{
		sstr.str ("");
		sstr << boost::json::serialize (tree);
		return sstr.str ();
	}

	// WARNING: This method moves the object out, leaving it in an undefined state
	boost::json::object && to_object ()
	{
		return std::move (tree);
	}

private:
	std::ostringstream sstr;
};

/** File sink with rotation support. This writes one counter per line and does not include histogram values. */
class stat_file_writer : public nano::stat_log_sink
{
public:
	std::ofstream log;
	std::string filename;

	explicit stat_file_writer (std::string const & filename) :
		filename (filename)
	{
		log.open (filename.c_str (), std::ofstream::out);
	}

	~stat_file_writer () override
	{
		log.close ();
	}

	std::ostream & out () override
	{
		return log;
	}

	void write_header (std::string const & header, std::chrono::system_clock::time_point & walltime) override
	{
		std::time_t now = std::chrono::system_clock::to_time_t (walltime);
		tm tm = *localtime (&now);
		log << header << "," << boost::format ("%04d.%02d.%02d %02d:%02d:%02d") % (1900 + tm.tm_year) % (tm.tm_mon + 1) % tm.tm_mday % tm.tm_hour % tm.tm_min % tm.tm_sec << std::endl;
	}

	void write_counter_entry (tm & tm, std::string const & type, std::string const & detail, std::string const & dir, uint64_t value) override
	{
		log << boost::format ("%02d:%02d:%02d") % tm.tm_hour % tm.tm_min % tm.tm_sec << "," << type << "," << detail << "," << dir << "," << value << std::endl;
	}

	void write_sampler_entry (tm & tm, const std::string & sample, const std::vector<stats::sampler_value_t> & values, std::pair<stats::sampler_value_t, stats::sampler_value_t> expected_min_max) override
	{
		log << boost::format ("%02d:%02d:%02d") % tm.tm_hour % tm.tm_min % tm.tm_sec << "," << sample;
		for (const auto & value : values)
		{
			log << "," << value;
		}
		log << std::endl;
	}

	void rotate () override
	{
		log.close ();
		log.open (filename.c_str (), std::ofstream::out);
		log_entries = 0;
	}
};
}
