#include "pch.h"
#include "winmain.h"

#ifdef __ANDROID__
#include <thread>

#include <cerrno>

#include <android/log.h>
#include <unistd.h>
// Renames main to SDL_main, which SDL's Java activity calls in libmain.so
#include <SDL3/SDL_main.h>

namespace
{
	// The game prints its progress to stdout, which Android drops; this sends both streams to logcat instead
	void LogStdoutToLogcat()
	{
		int pipeEnds[2];
		if (pipe(pipeEnds) != 0)
			return;
		setvbuf(stdout, nullptr, _IOLBF, 0);
		setvbuf(stderr, nullptr, _IONBF, 0);
		dup2(pipeEnds[1], STDOUT_FILENO);
		dup2(pipeEnds[1], STDERR_FILENO);
		close(pipeEnds[1]);
		std::thread{[readEnd = pipeEnds[0]]
					{
						std::string line;
						char buffer[512];
						while (true)
						{
							auto count = read(readEnd, buffer, sizeof buffer);
							// A signal handler without SA_RESTART interrupts the read; that isn't the end
							if (count < 0 && errno == EINTR)
								continue;
							if (count <= 0)
								break;
							for (ssize_t index = 0; index < count; index++)
							{
								if (buffer[index] != '\n')
									line += buffer[index];
								else
								{
									__android_log_write(ANDROID_LOG_INFO, "SpaceCadetPinball", line.c_str());
									line.clear();
								}
							}
						}
						// The last line, when the streams end without a newline
						if (!line.empty())
							__android_log_write(ANDROID_LOG_INFO, "SpaceCadetPinball", line.c_str());
					}}
			.detach();
	}
} // namespace
#endif

int main(int argc, char* argv[])
{
#ifdef __ANDROID__
	LogStdoutToLogcat();
#endif
	std::string cmdLine;
	for (int i = 1; i < argc; i++)
	{
		if (i > 1)
			cmdLine += " ";
		// Quoted, so that WinMain reads an argument with spaces as one, as it does on Windows
		if (strchr(argv[i], ' ') || strchr(argv[i], '\t'))
			cmdLine += std::string("\"") + argv[i] + "\"";
		else
			cmdLine += argv[i];
	}

	return winmain::WinMain(cmdLine.c_str());
}

#if _WIN32
#include <windows.h>

// Windows subsystem main
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	return winmain::WinMain(lpCmdLine);
}

// fopen to _wfopen adapter, for UTF-8 paths
FILE* fopenu(const char* path, const char* opt)
{
	wchar_t* wideArgs[2]{};
	for (auto& arg : wideArgs)
	{
		auto src = wideArgs[0] ? opt : path;
		auto length = MultiByteToWideChar(CP_UTF8, 0, src, -1, nullptr, 0);
		arg = new wchar_t[length];
		MultiByteToWideChar(CP_UTF8, 0, src, -1, arg, length);
	}

	auto fileHandle = _wfopen(wideArgs[0], wideArgs[1]);
	for (auto arg : wideArgs)
		delete[] arg;

	return fileHandle;
}
#endif

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
