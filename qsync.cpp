#include "qsync.h"

using namespace std;

void PrintFilesAndDirs(
    const vector<u8string>& Files,
    const vector<u8string>& Dirs)
{
    for (auto& File : Files) {
        cout << (char*)File.data() << endl;
    }
    cout << "+++++++++++++++++++++++++++" << endl;
    for (auto& Dir : Dirs) {
        cout << (char*)Dir.data() << endl;
    }
    cout << "---------------------------" << endl;
}

int main(
    int argc,
    char** argv)
{
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);
    printf("Hello world!\n");

    if (argc == 2) {
        vector<string> Files;
        vector<string> Dirs;
        if (!FindFiles(argv[1], PrintFilesAndDirs)) {
            cout << "Failed to finish parsing!" << endl;
        }
    }
    return 0;
}