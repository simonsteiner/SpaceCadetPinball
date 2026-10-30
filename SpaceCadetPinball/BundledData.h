#pragma once

// A test build's game data (the SCP_BUNDLED_DATA build option, which tools/make-test-build.sh sets), searched right
// after the player's chosen folder. Builds without the option have none.
class BundledData
{
public:
	// The folder that holds the data, ending in a separator, or empty when there is none. On Windows the data is in
	// the executable, and the first start unpacks it into prefPath, in a folder named after the data's hash; the web
	// page preloads it.
	static std::string Folder(const std::string& prefPath);
};
