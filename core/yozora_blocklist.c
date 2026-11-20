#include "yozora_blocklist.h"
#include <stdlib.h>
#include <string.h>

/* sorted by strcmp, the lookup is a binary search and a test checks the order. entries are registrable
   domains, a full host name limits a rule to one subdomain */
static const char *const blocked_domains[] = {
    "2mdn.net",
    "2o7.net",
    "33across.com",
    "360yield.com",
    "ad.mail.ru",
    "adform.net",
    "adfox.ru",
    "adition.com",
    "adnxs.com",
    "adriver.ru",
    "adroll.com",
    "ads-twitter.com",
    "adsafeprotected.com",
    "adservice.google.com",
    "adsrvr.org",
    "advertising.com",
    "agkn.com",
    "amazon-adsystem.com",
    "amplitude.com",
    "an.yandex.ru",
    "analytics.google.com",
    "analytics.tiktok.com",
    "analytics.twitter.com",
    "app-measurement.com",
    "appsflyer.com",
    "bat.bing.com",
    "begun.ru",
    "bidswitch.net",
    "bluekai.com",
    "casalemedia.com",
    "chartbeat.com",
    "chartbeat.net",
    "clarity.ms",
    "collector.github.com",
    "collector.githubapp.com",
    "contextweb.com",
    "counter.rambler.ru",
    "counter.yadro.ru",
    "crazyegg.com",
    "criteo.com",
    "criteo.net",
    "crwdcntrl.net",
    "ct.pinterest.com",
    "demdex.net",
    "doubleclick.net",
    "doubleverify.com",
    "everesttech.net",
    "exelator.com",
    "fullstory.com",
    "gemius.com",
    "gemius.pl",
    "google-analytics.com",
    "googleadservices.com",
    "googlesyndication.com",
    "googletagmanager.com",
    "googletagservices.com",
    "heapanalytics.com",
    "hotjar.com",
    "hotjar.io",
    "id5-sync.com",
    "indexww.com",
    "innovid.com",
    "krxd.net",
    "liadm.com",
    "lijit.com",
    "liveinternet.ru",
    "luckyorange.com",
    "marketgid.com",
    "mathtag.com",
    "mc.yandex.com",
    "mc.yandex.ru",
    "media.net",
    "mgid.com",
    "mixpanel.com",
    "moatads.com",
    "mouseflow.com",
    "mxpnl.com",
    "nr-data.net",
    "omtrdc.net",
    "openx.net",
    "outbrain.com",
    "permutive.com",
    "pippio.com",
    "pubmatic.com",
    "px.ads.linkedin.com",
    "quantserve.com",
    "relap.io",
    "revcontent.com",
    "rlcdn.com",
    "rubiconproject.com",
    "sc-static.net",
    "scorecardresearch.com",
    "segment.com",
    "segment.io",
    "serving-sys.com",
    "sharethrough.com",
    "smartadserver.com",
    "smi2.ru",
    "snap.licdn.com",
    "sovrn.com",
    "spotxchange.com",
    "static.ads-twitter.com",
    "taboola.com",
    "taboolasyndication.com",
    "tapad.com",
    "teads.tv",
    "tns-counter.ru",
    "top.mail.ru",
    "top100.rambler.ru",
    "tr.snapchat.com",
    "turn.com",
    "yabs.yandex.ru",
    "yandexadexchange.net",
};

#define BLOCKED_COUNT (sizeof(blocked_domains) / sizeof(blocked_domains[0]))
/* the longest valid host name */
#define HOST_LIMIT 253

static int compare_entry(const void *key, const void *item)
{
    return strcmp((const char *)key, *(const char *const *)item);
}

int yozora_blocklist_matches(const char *host, size_t length)
{
    char name[HOST_LIMIT + 1];
    if (!host)
        return 0;
    if (length && host[length - 1] == '.')
        --length;
    if (!length || length > HOST_LIMIT)
        return 0;
    for (size_t i = 0; i < length; ++i) {
        char c = host[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + ('a' - 'A'));
        /* a NUL inside the host would end the comparison early */
        if (!c)
            return 0;
        name[i] = c;
    }
    name[length] = 0;
    /* each suffix that starts at a label boundary, never the bare top-level label */
    const char *suffix = name;
    while (strchr(suffix, '.')) {
        if (bsearch(suffix, blocked_domains, BLOCKED_COUNT, sizeof(blocked_domains[0]), compare_entry))
            return 1;
        suffix = strchr(suffix, '.') + 1;
    }
    return 0;
}

size_t yozora_blocklist_count(void)
{
    return BLOCKED_COUNT;
}

const char *yozora_blocklist_entry(size_t index)
{
    return index < BLOCKED_COUNT ? blocked_domains[index] : NULL;
}
