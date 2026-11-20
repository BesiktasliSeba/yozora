#pragma once

namespace WebCore {
class Page;
void installYozoraPlatformStrategies();
// requests of a page in a history load may use stored copies without asking the server
void setYozoraHistoryLoad(const Page&, bool);
// requests of a page in the desktop mode neither read nor write the disk cache: a server may answer one
// address with another page for another user agent and the cache keys a response by address alone
void setYozoraDesktopPage(const Page&, bool);
// forgets the names and passwords that the user gave for sites in this run
void clearYozoraCredentials();
}
