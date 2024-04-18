#include <nano/lib/signal_manager.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/utility.hpp>

#include <boost/asio.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/format.hpp>

#include <csignal>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

nano::signal_manager::signal_manager () :
	work (boost::asio::make_work_guard (ioc))
{
	thread = std::thread ([&ioc = ioc] () {
		nano::thread_role::set (nano::thread_role::name::signal_manager);
		ioc.run ();
	});

#ifdef _WIN32
	SetConsoleCtrlHandler ([] (DWORD CtrlType) -> BOOL {
		switch (CtrlType)
		{
			case CTRL_C_EVENT:
			case CTRL_BREAK_EVENT:
			case CTRL_CLOSE_EVENT:
			case CTRL_LOGOFF_EVENT:
			case CTRL_SHUTDOWN_EVENT:
				std::cout << "Windows signal received!!";
				// Signal your shutdown here
				return TRUE; // Indicate that the handler has taken care of the signal
			default:
				return FALSE;
		}
	},
	TRUE);
#endif
}

nano::signal_manager::~signal_manager ()
{
	work.reset ();
	ioc.stop ();
	thread.join ();
}

void nano::signal_manager::register_signal_handler (int signum, std::function<void (int)> handler, bool repeat)
{
#ifndef _WIN32
	std::cout << "Signal received!!";
	auto sigset = std::make_shared<boost::asio::signal_set> (ioc, signum);
	signal_descriptor descriptor (sigset, *this, handler, repeat);
	descriptor_list.push_back (descriptor);
	sigset->async_wait ([descriptor] (boost::system::error_code const & error, int signum) {
		nano::signal_manager::base_handler (descriptor, error, signum);
	});
#endif
}

void nano::signal_manager::base_handler (nano::signal_manager::signal_descriptor descriptor, boost::system::error_code const & ec, int signum)
{
	auto & logger = descriptor.sigman.logger;
	std::cout << "Signal received 2!!";
	if (!ec)
	{
		logger.debug (nano::log::type::signal_manager, "Signal received: {}", to_signal_name (signum));
		if (descriptor.handler_func)
		{
			descriptor.handler_func (signum);
		}
		if (descriptor.repeat)
		{
			descriptor.sigset->async_wait ([descriptor] (boost::system::error_code const & error, int signum) {
				nano::signal_manager::base_handler (descriptor, error, signum);
			});
		}
		else
		{
			logger.debug (nano::log::type::signal_manager, "Signal handler {} will not repeat", to_signal_name (signum));
			descriptor.sigset->clear ();
		}
	}
	else
	{
		logger.error (nano::log::type::signal_manager, "Signal error: {} ({})", ec.message (), to_signal_name (signum));
	}
}
