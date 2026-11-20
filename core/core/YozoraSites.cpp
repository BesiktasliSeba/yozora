#include "config.h"
#include "YozoraSites.h"
#include "YozoraPrivateSession.h"
#include "../yozora_log.h"
#include <cerrno>
#include <cstring>
#include <pthread.h>

namespace {

constexpr size_t kind_count = 3;
// a person does not decide for more sites than this, the oldest decision goes first
constexpr size_t site_limit = 512;

struct Table {
    const char* header;
    const char* category;
    yozora_sitegrants* grants { nullptr };
    std::string path;
};

Table tables[kind_count] = {
    { "yozora-geolocation 1", "geo", nullptr, { } },
    // the header of the file the notifications wrote before this module, so the decisions that exist stay valid
    { "yozora-notifications 1", "notify", nullptr, { } },
    { "yozora-adblock-sites 1", "adblock", nullptr, { } },
};

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// with the lock held. the table is made on first use, NULL when memory is out
Table* table_of(yozora_site_kind kind)
{
    if (static_cast<int>(kind) < 0 || static_cast<size_t>(kind) >= kind_count)
        return nullptr;
    Table& table = tables[kind];
    if (!table.grants)
        table.grants = yozora_sitegrants_create(table.header, site_limit);
    return table.grants ? &table : nullptr;
}

// the origin of a sandboxed frame or a local file is written "null", the same text for every such page, so no
// decision is kept for it
bool usable(const std::string& key)
{
    return !key.empty() && key != "null";
}

void save(Table& table)
{
    if (table.path.empty())
        return;
    if (yozora_sitegrants_save(table.grants, table.path.c_str()) != 0)
        yozora_log(YOZORA_LOG_ERROR, table.category, "decisions cannot be saved: %s", std::strerror(errno));
}

}

namespace YozoraSites {

void setFile(yozora_site_kind kind, const std::string& path)
{
    pthread_mutex_lock(&lock);
    if (Table* table = table_of(kind)) {
        table->path = path;
        if (!path.empty() && yozora_sitegrants_load(table->grants, path.c_str()) != 0)
            yozora_log(YOZORA_LOG_ERROR, table->category, "decisions cannot be read: %s", std::strerror(errno));
    }
    pthread_mutex_unlock(&lock);
}

yozora_grant get(yozora_site_kind kind, const std::string& key)
{
    yozora_grant result = YOZORA_GRANT_NONE;
    if (!usable(key))
        return result;
    pthread_mutex_lock(&lock);
    if (Table* table = table_of(kind))
        result = yozora_sitegrants_get(table->grants, key.c_str());
    pthread_mutex_unlock(&lock);
    return result;
}

yozora_grant getForHost(yozora_site_kind kind, const std::string& host)
{
    yozora_grant result = YOZORA_GRANT_NONE;
    pthread_mutex_lock(&lock);
    if (Table* table = table_of(kind)) {
        size_t at = 0;
        // each suffix that starts at a label, the last label alone is not a site
        while (result == YOZORA_GRANT_NONE && host.find('.', at) != std::string::npos) {
            result = yozora_sitegrants_get(table->grants, host.c_str() + at);
            at = host.find('.', at) + 1;
        }
    }
    pthread_mutex_unlock(&lock);
    return result;
}

bool remember(yozora_site_kind kind, const std::string& key, yozora_grant grant)
{
    bool stored = false;
    if (YozoraPrivateSession::active() || !usable(key))
        return false;
    pthread_mutex_lock(&lock);
    if (Table* table = table_of(kind)) {
        stored = yozora_sitegrants_set(table->grants, key.c_str(), grant) == 0;
        if (stored)
            save(*table);
    }
    pthread_mutex_unlock(&lock);
    return stored;
}

}

extern "C" size_t yozora_site_count(yozora_site_kind kind)
{
    size_t count = 0;
    pthread_mutex_lock(&lock);
    if (Table* table = table_of(kind))
        count = yozora_sitegrants_count(table->grants);
    pthread_mutex_unlock(&lock);
    return count;
}

extern "C" int yozora_site_entry(yozora_site_kind kind, size_t index, char* key, size_t capacity, int* allowed)
{
    int found = 0;
    if (!key || !capacity)
        return 0;
    key[0] = '\0';
    pthread_mutex_lock(&lock);
    if (Table* table = table_of(kind)) {
        // newest first: the list of the interface shows what was decided last at the top
        size_t count = yozora_sitegrants_count(table->grants);
        if (index < count) {
            size_t at = count - 1 - index;
            const char* name = yozora_sitegrants_key_at(table->grants, at);
            if (name && std::strlen(name) < capacity) {
                std::memcpy(key, name, std::strlen(name) + 1);
                if (allowed)
                    *allowed = yozora_sitegrants_grant_at(table->grants, at) == YOZORA_GRANT_ALLOW ? 1 : 0;
                found = 1;
            }
        }
    }
    pthread_mutex_unlock(&lock);
    return found;
}

extern "C" void yozora_site_remove(yozora_site_kind kind, const char* key)
{
    if (!key)
        return;
    pthread_mutex_lock(&lock);
    if (Table* table = table_of(kind)) {
        if (yozora_sitegrants_remove(table->grants, key))
            save(*table);
    }
    pthread_mutex_unlock(&lock);
}

extern "C" void yozora_site_clear(yozora_site_kind kind)
{
    pthread_mutex_lock(&lock);
    if (Table* table = table_of(kind)) {
        yozora_sitegrants_clear(table->grants);
        save(*table);
    }
    pthread_mutex_unlock(&lock);
}
