#pragma once

// A test build's game data (the SCP_BUNDLED_DATA build option, which tools/make-test-build.sh sets, or on Android the
// APK's assets), searched right after the player's chosen folder. Builds without it have none.
class BundledData
{
public:
	// The folder that holds the data, ending in a separator, or empty when there is none. On Windows the data is in
	// the executable, and the first start unpacks it into prefPath, in a folder named after the data's hash; the web
	// page preloads it; on Android the first start copies the APK's bundled/ assets into prefPath in the same way.
	static std::string Folder(const std::string& prefPath);
};
