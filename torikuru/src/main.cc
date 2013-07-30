/* RFA 7.4 subscriber nee consumer.
 */

#include "torikuru.hh"

#include <cstdlib>

/* Google Protocol Buffers */
#include <google/protobuf/stubs/common.h>

#include "chromium/chromium_switches.hh"
#include "chromium/command_line.hh"
#include "chromium/logging.hh"

class env_t
{
public:
	env_t (int argc, const char* argv[])
	{
/* startup from clean string */
		CommandLine::Init (argc, argv);
/* forward onto logging */
		logging::InitLogging(
			nullptr,
			logging::LOG_NONE,
			logging::DONT_LOCK_LOG_FILE,
			logging::APPEND_TO_OLD_LOG_FILE,
			logging::ENABLE_DCHECK_FOR_NON_OFFICIAL_RELEASE_BUILDS
			);
		logging::SetLogMessageHandler (log_handler);
	}

protected:
	std::string GetLogFileName() {
		const std::string log_filename ("/Torikuru.log");
		return log_filename;
	}

	logging::LoggingDestination DetermineLogMode (const CommandLine& command_line) {
#ifdef NDEBUG
		const logging::LoggingDestination kDefaultLoggingMode = logging::LOG_NONE;
#else
		const logging::LoggingDestination kDefaultLoggingMode = logging::LOG_TO_BOTH_FILE_AND_SYSTEM_DEBUG_LOG;
#endif

		logging::LoggingDestination log_mode;
// Let --enable-logging=file force Vhayu and file logging, particularly useful for
// non-debug builds where otherwise you can't get logs on fault at all.
		if (command_line.GetSwitchValueASCII (switches::kEnableLogging) == "file")
			log_mode = logging::LOG_ONLY_TO_FILE;
		else
			log_mode = kDefaultLoggingMode;
		return log_mode;
	}

	static bool log_handler (int severity, const char* file, int line, size_t message_start, const std::string& str)
	{
		fprintf (stdout, "%s", str.c_str());
		fflush (stdout);
/* allow additional log targets */
		return false;
	}
};

int
main (
	int		argc,
	const char*	argv[]
	)
{
#ifdef _MSC_VER
/* Suppress abort message. */
	_set_abort_behavior (0, ~0);
#endif

	env_t env (argc, argv);

// Verify that the version of the library that we linked against is
// compatible with the version of the headers we compiled against.
	GOOGLE_PROTOBUF_VERIFY_VERSION;

	torikuru::torikuru_t torikuru;
	return torikuru.Run();
}

/* eof */
