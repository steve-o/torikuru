/* RFA context.
 */

#include "rfa.hh"

#include <cassert>

#include "chromium/logging.hh"
#include "chromium/string_util.hh"
#include "deleter.hh"
#include "rfaostream.hh"

using rfa::common::RFA_String;

static const char* kAppName = "Torikuru";

namespace connections {
const char kSSLED[] = "ssled";
const char kRSSL[] = "rssl";
}

static const RFA_String kContextName ("RFA");

/* Translate forward slashes into backward slashes for broken Rfa library.
 */
static
void
fix_rfa_string_path (
	RFA_String*	rfa_str
	)
{
#ifndef RFA_HIVE_ABBREVIATION_FIXED
/* RFA string API is hopeless, use std library. */
	std::string str (rfa_str->c_str());
	if (0 == str.compare (0, 2, "HK")) {
		if (0 == str.compare (2, 2, "LM"))
			str.replace (2, 2, "EY_LOCAL_MACHINE");
		else if (0 == strncmp (str.c_str(), "HKCC", 4))
			str.replace (2, 2, "EY_CURRENT_CONFIG");
		else if (0 == strncmp (str.c_str(), "HKCR", 4))
			str.replace (2, 2, "EY_CLASSES_ROOT");
		else if (0 == strncmp (str.c_str(), "HKCU", 4))
			str.replace (2, 2, "EY_CURRENT_USER");
		else if (0 == strncmp (str.c_str(), "HKU", 3))
			str.replace (2, 2, "EY_USERS");
		rfa_str->set (str.c_str());
	}
#endif
#ifndef RFA_FORWARD_SLASH_IN_PATH_FIXED
	size_t pos = 0;
	while (-1 != (pos = rfa_str->find ("/", (unsigned)pos)))
		rfa_str->replace ((unsigned)pos++, 1, "\\");
#endif
}

namespace rfa {
namespace config {
	
inline
std::ostream& operator<< (std::ostream& o, const ConfigTree& config_tree)
{
	o << "\n[HKEY_LOCAL_MACHINE\\SOFTWARE\\Reuters\\RFA\\" << kAppName << config_tree.getFullName() << "]\n";
	auto pIt = config_tree.createIterator();
	CHECK(pIt);
	for (pIt->start(); !pIt->off(); pIt->forth()) {
		auto pConfigNode = pIt->value();
		switch (pConfigNode->getType()) {
		case treeNode:
			o << *static_cast<const ConfigTree*> (pConfigNode);
			break;
		case longValueNode:
			o << '"' << pConfigNode->getNodename() << "\""
				"=dword:" << std::hex << static_cast<const ConfigLong*> (pConfigNode)->getValue() << "\n";
			break;
		case boolValueNode:
			o << '"' << pConfigNode->getNodename() << "\""
				"=\"" << (static_cast<const ConfigBool*> (pConfigNode)->getValue() ? "true" : "false") << "\"\n";
			break;
		case stringValueNode:
			o << '"' << pConfigNode->getNodename() << "\""
				"=\"" << static_cast<const ConfigString*> (pConfigNode)->getValue() << "\"\n";
			break;
		case wideStringValueNode:
		case stringListValueNode:
		case wideStringListValueNode:
		case softlinkNode:
		default:
			o << '"' << pConfigNode->getNodename() << "\"=<other type>\n";
			break;
		}
	}
	pIt->destroy();
	return o;
}

} // config
} // rfa

torikuru::rfa_t::rfa_t (const config_t& config) :
	config_ (config)
{
}

torikuru::rfa_t::~rfa_t()
{
	VLOG(2) << "Closing RFA.";
	rfa_config_.reset();
	rfa::common::Context::uninitialize();
}

bool
torikuru::rfa_t::Init()
throw (rfa::common::InvalidUsageException)
{
	VLOG(2) << "Initializing RFA.";
	rfa::common::Context::initialize();

/* 8.2.3 Populate Config Database.
 */
	VLOG(3) << "Populating RFA config database.";
	std::unique_ptr<rfa::config::StagingConfigDatabase, internal::destroy_deleter> staging (rfa::config::StagingConfigDatabase::create());
	if (!(bool)staging)
		return false;

/* Disable Windows Event Logger. */
	RFA_String name, value;

/* Disable Windows Event Logger. */
	name.set ("/Logger/AppLogger/windowsLoggerEnabled");
	fix_rfa_string_path (&name);
	staging->setBool (name, false);

/* Disable File Logger. */
	name.set ("/Logger/AppLogger/fileLoggerEnabled");
	fix_rfa_string_path (&name);
	staging->setBool (name, false);

/* Disable external message file support. */
	name.set ("/Logger/AppLogger/useInternalLogStrings");
	fix_rfa_string_path (&name);
	staging->setBool (name, true);

	bool disable_entitlements = false;
	bool load_mfeed_dictionary = false;
	for (const auto& session_config : config_.sessions)
	{
		const RFA_String session_name (session_config.session_name.c_str(), 0, false),
			connection_name (session_config.connection_name.c_str(), 0, false);

/* Session list */
		name = "/Sessions/" + session_name + "/connectionList";
		fix_rfa_string_path (&name);
		staging->setString (name, connection_name);
/* List of servers */
		name = "/Connections/" + connection_name + "/serverList";
		fix_rfa_string_path (&name);
		const std::string servers (JoinString (session_config.servers, ','));
		value.set (servers.c_str());
		staging->setString (name, value);

		if (LowerCaseEqualsASCII (session_config.protocol, connections::kRSSL))
		{
/* Connection list */
			name = "/Connections/" + connection_name + "/connectionType";
			fix_rfa_string_path (&name);
			value.set ("RSSL");
			staging->setString (name, value);
/* Default RSSL port as string */
			if (!session_config.default_port.empty()) {
				name = "/Connections/" + connection_name + "/rsslPort";
				fix_rfa_string_path (&name);
				value.set (session_config.default_port.c_str());
				staging->setString (name, value);
			}
		} 
		else if (LowerCaseEqualsASCII (session_config.protocol, connections::kSSLED))
		{
/* Connection list */
			name = "/Connections/" + connection_name + "/connectionType";
			fix_rfa_string_path (&name);
			value.set ("SSLED");
			staging->setString (name, value);
/* Default SSL port as integer */
			if (!session_config.default_port.empty()) {
				name = "/Connections/" + connection_name + "/portNumber";
				fix_rfa_string_path (&name);
				staging->setLong (name, std::atoi (session_config.default_port.c_str()));
			}
			name = "/Connections/" + connection_name + "/userName";
			fix_rfa_string_path (&name);
			value.set (session_config.user_name.c_str());
			if (!session_config.application_id.empty()) {
				value.append ('+');
				value.append (session_config.application_id.c_str());
				if (!session_config.position.empty()) {
					value.append ('+');
					value.append (session_config.position.c_str());
				}
			}
			staging->setString (name, value);
			disable_entitlements = true;
			load_mfeed_dictionary = true;
		}
		else
		{
			LOG(ERROR) << "Unsupported transport protocol \"" << session_config.protocol << "\".";
			return false;
		}
	}

	if (disable_entitlements) {
/* Disable entitlements */
		name = "/Control/Entitlements/dacs_CbeEnabled";
		fix_rfa_string_path (&name);
		staging->setBool (name, false);
		name = "/Control/Entitlements/dacs_SbeEnabled";
		fix_rfa_string_path (&name);
		staging->setBool (name, false);
	}
	if (load_mfeed_dictionary) {
/* Local dictionary */
		TibErr err = TibMsg::ReadMfeedDictionary ("./appendix_a", "enumtype.def");
		if (err != TIBMSG_OK) {
			LOG(ERROR) << "Failed to read market feed dictionary from appendix_a and enumtype.def.";
			return false;
		}
	}

	rfa_config_.reset (rfa::config::ConfigDatabase::acquire (kContextName));
	if (!(bool)rfa_config_)
		return false;

	VLOG(3) << "Merging RFA config database with staging database.";
	if (!rfa_config_->merge (*staging.get()))
		return false;

	VLOG(3) << "RFA initialization complete.";
	return true;
}

bool
torikuru::rfa_t::VerifyVersion()
{
/* 6.2.2.1 RFA Version Info.  The version is only available if an application
 * has acquired a Session (i.e., the Session Layer library is loaded).
 */
	const auto runtimeVersion = rfa::common::Context::getRFAVersionInfo()->getProductVersion();
/* Emits compiler warning as RFA_String does not use size_t for sizes. */
	if (runtimeVersion.substr (0, (unsigned)strlen (RFA_LIBRARY_VERSION)).compareCase (RFA_LIBRARY_VERSION, (unsigned)strlen (RFA_LIBRARY_VERSION))) {
// Library is too old for headers.
		LOG(FATAL)
		<< "This program requires version " RFA_LIBRARY_VERSION
		    " of the RFA runtime library, but the installed version "
		   "is " << runtimeVersion << ".  Please update "
		   "your library.  If you compiled the program yourself, make sure that "
		   "your headers are from the same version of RFA as your "
		   "link-time library.";
		return false;
	}
	LOG(INFO) << "RFA: { \"productVersion\": \"" << runtimeVersion << "\" }";
	return true;
}

/* eof */
