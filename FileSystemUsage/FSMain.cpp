#include <Poco/DirectoryIterator.h>
#include <Poco/DateTimeFormatter.h>
#include <Poco/LocalDateTime.h>
#include <iostream>
#include <string>

using namespace Poco;
using namespace std;

void rec_dir(const string & path)
{
  DirectoryIterator end;
  for (DirectoryIterator it(path); it != end; ++it)
  {
    cout << (it->isDirectory() ? "d" : "-");
    cout << (it->canRead()     ? "r" : "-");
    cout << (it->canWrite()    ? "w" : "-");
    cout << (it->canExecute()  ? "x" : "-");
    cout << "\t";

    cout << it->getSize() << "\t";

    LocalDateTime lastModified(it->getLastModified());
    cout << DateTimeFormatter::format(lastModified, "%Y-%m-%d %H:%M") << "\t";

    cout << it->path() << (it->isDirectory() ? "/" : it->canExecute() ? "*" : "") << endl;

	if (it->isDirectory()&&!it->isJunction())
    {
      rec_dir(it->path());
    }
  }
}

int main(int argc, char **argv)
{
  rec_dir(argc > 1 ? argv[1] : ".");
  return 0;
}