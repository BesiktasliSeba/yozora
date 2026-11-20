#pragma once

#include "yozora_core.h"
#include "yozora_sitegrants.h"
#include <string>

// what the person decided for each site, one table per kind (see yozora_site_kind): geolocation and
// notifications of an origin, the blocker pause for a host. shared by the tabs and read from the core thread
// and the interface, so each access takes the module lock
namespace YozoraSites {

// the file of a kind, read at once. an empty path keeps the table in memory
void setFile(yozora_site_kind, const std::string& path);

yozora_grant get(yozora_site_kind, const std::string& key);
// the grant of the host or of the nearest parent of it that has one, never of the bare last label
yozora_grant getForHost(yozora_site_kind, const std::string& host);
// stores and saves the decision. in the private session nothing is stored, the site asks again, and false comes back
bool remember(yozora_site_kind, const std::string& key, yozora_grant);

}
