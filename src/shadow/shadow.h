#include "../unicode.h"

#include <string>

using namespace std;

int shadow(vector<STRING> vol);
STRING snap(STRING path);
STRING unsnap(STRING path);

void unshadow(void);
STRING DisplayVolumePaths(__in PWCHAR VolumeName);
STRING snappart(STRING path);
STRING volpart(STRING path);
vector<pair<STRING, STRING> > get_snaps(void);