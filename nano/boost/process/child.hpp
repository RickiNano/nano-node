#pragma once

#include <nano/boost/private/macro_warnings.hpp>

DISABLE_PROCESS_WARNINGS
#include <boost/asio/io_context.hpp>
#include <boost/process/v2/process.hpp>
REENABLE_WARNINGS

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace nano::process
{
/*
 * Drop-in replacement for the Boost.Process v1 `boost::process::child` that was
 * removed in Boost 1.88. Boost.Process v2 requires an execution context to launch
 * a process, so this wrapper owns a dedicated `io_context`. The context is never
 * run: `wait()` and `terminate()` operate synchronously via the OS, which does not
 * require the context to be polled.
 *
 * Note: unlike Boost.Process v1, v2 does NOT search PATH for the executable. All
 * existing call sites pass a fully-resolved path, so this is intentional.
 */
class child final
{
public:
	template <typename... Args>
	explicit child (std::string exe, Args &&... args) :
		ctx{ std::make_unique<boost::asio::io_context> () },
		proc{ *ctx,
			std::filesystem::path{ std::move (exe) },
			std::vector<std::string>{ std::string (std::forward<Args> (args))... } }
	{
	}

	child (child const &) = delete;
	child & operator= (child const &) = delete;

	int wait ()
	{
		return proc.wait ();
	}

	void terminate ()
	{
		proc.terminate ();
	}

	bool running ()
	{
		return proc.running ();
	}

private:
	// Declared before `proc` so it outlives the process on destruction.
	std::unique_ptr<boost::asio::io_context> ctx;
	boost::process::v2::process proc;
};
}
