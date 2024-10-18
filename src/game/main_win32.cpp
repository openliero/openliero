#include <windows.h>
#include <exception>
#include <string>

int gameEntry(int argc, char* argv[]);

INT WINAPI WinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    PSTR lpCmdLine,
    INT nCmdShow) {
  try {
    return gameEntry(__argc, __argv);
  } catch (std::exception& ex) {
    MessageBoxA(
        NULL,
        (std::string("Sorry, something went wrong :(\r\n\r\n") + ex.what())
            .c_str(),
        "Liero", MB_OK | MB_ICONWARNING);
    return 1;
  }
}