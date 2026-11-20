#include "yozora_filter.h"
#include "yozora_blocklist.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define HOST_LIMIT 253
#define PATTERN_MAX 512
#define SELECTOR_MAX 512
#define DOMAINS_MAX 256
#define TYPE_BIT(type) ((uint16_t)(1u << (type)))
#define ALL_TYPES ((uint16_t)0x7ff)

typedef struct {
    char *pattern;
    uint16_t length;
    /* the longest run of the pattern without * and ^, looked for in the address before the pattern is tried */
    uint16_t literal_offset;
    uint16_t literal_length;
    /* the place of the first three letters of the literal in a bit table of the address, valid when the literal has three */
    uint16_t trigram;
    uint8_t anchor_start;
    uint8_t anchor_end;
    uint8_t exception;
    /* 0 any request, 1 only third party requests, 2 only first party requests */
    uint8_t third;
    /* the types the rule names, 0 for the default of every type but the document, and the types it leaves out */
    uint16_t types_on;
    uint16_t types_off;
    char *domains;
} network_rule;

typedef struct {
    char *selector;
    char *domains;
    uint8_t exception;
} hiding_rule;

struct yozora_filter {
    network_rule *rules;
    size_t rule_count;
    size_t rule_capacity;
    /* rules of the form ||host^ without options, sorted for a binary search */
    char **hosts;
    size_t host_count;
    size_t host_capacity;
    /* sites where nothing is blocked or hidden (@@||site^$document) */
    char **sites;
    size_t site_count;
    size_t site_capacity;
    hiding_rule *hiding;
    size_t hiding_count;
    size_t hiding_capacity;
    int sorted;
};

static char *duplicate(const char *text, size_t length)
{
    char *copy = malloc(length + 1);
    if (!copy)
        return NULL;
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

yozora_filter *yozora_filter_create(void)
{
    return calloc(1, sizeof(yozora_filter));
}

static void free_strings(char **list, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        free(list[i]);
    free(list);
}

void yozora_filter_destroy(yozora_filter *filter)
{
    if (!filter)
        return;
    for (size_t i = 0; i < filter->rule_count; ++i) {
        free(filter->rules[i].pattern);
        free(filter->rules[i].domains);
    }
    free(filter->rules);
    free_strings(filter->hosts, filter->host_count);
    free_strings(filter->sites, filter->site_count);
    for (size_t i = 0; i < filter->hiding_count; ++i) {
        free(filter->hiding[i].selector);
        free(filter->hiding[i].domains);
    }
    free(filter->hiding);
    free(filter);
}

size_t yozora_filter_rule_count(const yozora_filter *filter)
{
    return filter ? filter->rule_count + filter->host_count + filter->site_count : 0;
}

size_t yozora_filter_hiding_count(const yozora_filter *filter)
{
    return filter ? filter->hiding_count : 0;
}

int yozora_filter_host_in_domain(const char *host, size_t host_length, const char *domain, size_t domain_length)
{
    if (!host || !domain || !domain_length || host_length < domain_length)
        return 0;
    if (host_length > domain_length && host[host_length - domain_length - 1] != '.')
        return 0;
    for (size_t i = 0; i < domain_length; ++i)
        if (lower(host[host_length - domain_length + i]) != lower(domain[i]))
            return 0;
    return 1;
}

static int is_host_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
}

/* "a.com|~b.com" against a host: in one of the named domains (when any is named) and in none of the left out */
static int domains_allow(const char *domains, const char *host, size_t host_length)
{
    int named = 0;
    int in_named = 0;
    const char *p = domains;
    while (*p) {
        const char *end = strchr(p, '|');
        size_t length = end ? (size_t)(end - p) : strlen(p);
        int out = length && p[0] == '~';
        const char *name = out ? p + 1 : p;
        size_t name_length = out ? length - 1 : length;
        if (name_length) {
            int inside = yozora_filter_host_in_domain(host, host_length, name, name_length);
            if (out && inside)
                return 0;
            if (!out) {
                named = 1;
                if (inside)
                    in_named = 1;
            }
        }
        p = end ? end + 1 : p + length;
    }
    return !named || in_named;
}

/* a domain list of an option or hiding rule, lowercased: letters, digits, dots and dashes, each with a ~ or
   not, joined by the separator */
static int normalize_domains(const char *text, size_t length, char separator, char **out)
{
    char *copy;
    size_t i = 0;
    size_t n = 0;
    *out = NULL;
    if (!length)
        return 1;
    if (length > DOMAINS_MAX)
        return 0;
    copy = malloc(length + 1);
    if (!copy)
        return -1;
    while (i < length) {
        size_t name = 0;
        if (text[i] == '~')
            copy[n++] = text[i++];
        while (i < length && text[i] != separator) {
            char c = lower(text[i]);
            if (!is_host_char(c)) {
                free(copy);
                return 0;
            }
            copy[n++] = c;
            ++i;
            ++name;
        }
        if (!name) {
            free(copy);
            return 0;
        }
        if (i < length) {
            copy[n++] = '|';
            ++i;
            if (i == length) {
                free(copy);
                return 0;
            }
        }
    }
    copy[n] = '\0';
    *out = copy;
    return 1;
}

static int grow(void **list, size_t *capacity, size_t count, size_t item, size_t limit)
{
    void *grown;
    size_t wanted;
    if (count < *capacity)
        return 1;
    wanted = *capacity ? *capacity * 2 : 32;
    if (wanted > limit)
        wanted = limit;
    if (wanted <= count)
        return 0;
    grown = realloc(*list, wanted * item);
    if (!grown)
        return 0;
    *list = grown;
    *capacity = wanted;
    return 1;
}

static int compare_host(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

void yozora_filter_finish(yozora_filter *filter)
{
    if (!filter)
        return;
    if (filter->host_count > 1)
        qsort(filter->hosts, filter->host_count, sizeof filter->hosts[0], compare_host);
    filter->sorted = 1;
}

static int host_listed(char *const *list, size_t count, int sorted, const char *host, size_t length)
{
    char name[HOST_LIMIT + 1];
    const char *suffix;
    if (!count || !length || length > HOST_LIMIT)
        return 0;
    for (size_t i = 0; i < length; ++i) {
        name[i] = lower(host[i]);
        if (!name[i])
            return 0;
    }
    name[length] = '\0';
    suffix = name;
    while (*suffix) {
        if (sorted) {
            if (bsearch(&suffix, list, count, sizeof list[0], compare_host))
                return 1;
        } else {
            for (size_t i = 0; i < count; ++i)
                if (!strcmp(list[i], suffix))
                    return 1;
        }
        suffix = strchr(suffix, '.');
        if (!suffix)
            break;
        ++suffix;
    }
    return 0;
}

static int add_name(char ***list, size_t *count, size_t *capacity, const char *name, size_t length, size_t limit)
{
    char *copy;
    if (!grow((void **)list, capacity, *count, sizeof(*list)[0], limit))
        return 0;
    copy = duplicate(name, length);
    if (!copy)
        return 0;
    (*list)[(*count)++] = copy;
    return 1;
}

/* the part of a pattern that is only text */
static uint16_t trigram_of(const char *text)
{
    return (uint16_t)((((unsigned char)text[0] * 131u + (unsigned char)text[1]) * 131u + (unsigned char)text[2]) & 0xffffu);
}

static void find_literal(network_rule *rule)
{
    size_t best = 0;
    size_t best_at = 0;
    size_t i = 0;
    while (i < rule->length) {
        size_t start = i;
        while (i < rule->length && rule->pattern[i] != '*' && rule->pattern[i] != '^')
            ++i;
        if (i - start > best) {
            best = i - start;
            best_at = start;
        }
        ++i;
    }
    rule->literal_offset = (uint16_t)best_at;
    rule->literal_length = (uint16_t)best;
    if (best >= 3)
        rule->trigram = trigram_of(rule->pattern + best_at);
}

typedef struct {
    uint16_t types_on;
    uint16_t types_off;
    uint8_t third;
    char *domains;
} option_set;

static int type_of_option(const char *name, size_t length)
{
    static const struct {
        const char *name;
        yozora_filter_type type;
    } types[] = {
        { "script", YOZORA_FILTER_SCRIPT }, { "image", YOZORA_FILTER_IMAGE }, { "stylesheet", YOZORA_FILTER_STYLESHEET },
        { "font", YOZORA_FILTER_FONT }, { "media", YOZORA_FILTER_MEDIA }, { "xmlhttprequest", YOZORA_FILTER_XHR },
        { "xhr", YOZORA_FILTER_XHR }, { "subdocument", YOZORA_FILTER_SUBDOCUMENT }, { "ping", YOZORA_FILTER_PING },
        { "websocket", YOZORA_FILTER_WEBSOCKET }, { "other", YOZORA_FILTER_OTHER }, { "document", YOZORA_FILTER_DOCUMENT },
        { "doc", YOZORA_FILTER_DOCUMENT }
    };
    for (size_t i = 0; i < sizeof types / sizeof types[0]; ++i)
        if (strlen(types[i].name) == length && !memcmp(types[i].name, name, length))
            return (int)types[i].type;
    return -1;
}

/* 1 read, 0 an option this engine does not do, -1 memory out. a malformed domain= is 0 as well: the rule is not used */
static int parse_options(const char *text, size_t length, option_set *options)
{
    size_t i = 0;
    memset(options, 0, sizeof *options);
    while (i <= length) {
        size_t end = i;
        const char *name;
        size_t name_length;
        int negated;
        int type;
        while (end < length && text[end] != ',')
            ++end;
        negated = end > i && text[i] == '~';
        name = text + i + (negated ? 1 : 0);
        name_length = end - i - (negated ? 1 : 0);
        if (!name_length)
            goto bad;
        if (name_length == 11 && !memcmp(name, "third-party", 11))
            options->third = negated ? 2 : 1;
        else if (name_length == 2 && !memcmp(name, "3p", 2))
            options->third = negated ? 2 : 1;
        else if (name_length == 2 && !memcmp(name, "1p", 2))
            options->third = negated ? 1 : 2;
        else if (name_length == 9 && !memcmp(name, "important", 9))
            ;
        else if (name_length == 10 && !memcmp(name, "match-case", 10))
            ;
        else if (!negated && name_length > 7 && !memcmp(name, "domain=", 7)) {
            int status;
            free(options->domains);
            options->domains = NULL;
            status = normalize_domains(name + 7, name_length - 7, '|', &options->domains);
            if (status < 0)
                return -1;
            if (!status || !options->domains)
                goto bad;
        } else if ((type = type_of_option(name, name_length)) >= 0) {
            if (negated)
                options->types_off |= TYPE_BIT(type);
            else
                options->types_on |= TYPE_BIT(type);
        } else
            goto bad;
        i = end + 1;
    }
    return 1;
bad:
    free(options->domains);
    options->domains = NULL;
    return 0;
}

/* a pattern that is only a host (and a closing ^): the form that goes to the sorted list */
static int pattern_is_host(const char *pattern, size_t length)
{
    if (length && pattern[length - 1] == '^')
        --length;
    if (length < 3 || length > HOST_LIMIT || !strchr(pattern, '.'))
        return 0;
    for (size_t i = 0; i < length; ++i)
        if (!is_host_char(pattern[i]))
            return 0;
    return pattern[0] != '.' && pattern[length - 1] != '.';
}

static int looks_like_regex(const char *pattern, size_t length)
{
    if (length < 3 || pattern[0] != '/' || pattern[length - 1] != '/')
        return 0;
    for (size_t i = 1; i + 1 < length; ++i)
        if (strchr("\\()[]{}+?|$", pattern[i]))
            return 1;
    return 0;
}

static int has_option_tail(const char *line, size_t length, size_t *dollar)
{
    size_t at = length;
    while (at > 0 && line[at - 1] != '$')
        --at;
    if (!at)
        return 0;
    --at;
    for (size_t i = at + 1; i < length; ++i) {
        char c = line[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '~' || c == ',' || c == '=' || c == '|' || c == '.' || c == '-'))
            return 0;
    }
    if (at + 1 >= length)
        return 0;
    *dollar = at;
    return 1;
}

static yozora_filter_status add_network(yozora_filter *filter, const char *line, size_t length)
{
    network_rule rule;
    option_set options;
    size_t dollar;
    int exception = 0;
    int status;
    size_t start = 0;
    char pattern[PATTERN_MAX + 1];
    size_t plength;
    memset(&rule, 0, sizeof rule);
    memset(&options, 0, sizeof options);
    if (length >= 2 && line[0] == '@' && line[1] == '@') {
        exception = 1;
        start = 2;
    }
    if (has_option_tail(line + start, length - start, &dollar)) {
        status = parse_options(line + start + dollar + 1, length - start - dollar - 1, &options);
        if (status < 0)
            return YOZORA_FILTER_NOMEM;
        if (!status)
            return YOZORA_FILTER_UNSUPPORTED;
        length = start + dollar;
    }
    plength = length - start;
    if (plength > PATTERN_MAX) {
        free(options.domains);
        return YOZORA_FILTER_INVALID;
    }
    for (size_t i = 0; i < plength; ++i)
        pattern[i] = lower(line[start + i]);
    pattern[plength] = '\0';
    if (looks_like_regex(pattern, plength)) {
        free(options.domains);
        return YOZORA_FILTER_UNSUPPORTED;
    }
    {
        const char *body = pattern;
        size_t body_length = plength;
        if (body_length >= 2 && body[0] == '|' && body[1] == '|') {
            rule.anchor_start = 2;
            body += 2;
            body_length -= 2;
        } else if (body_length && body[0] == '|') {
            rule.anchor_start = 1;
            ++body;
            --body_length;
        }
        if (body_length && body[body_length - 1] == '|') {
            rule.anchor_end = 1;
            --body_length;
        }
        /* a rule of nothing but wildcards would match every address */
        {
            size_t useful = 0;
            for (size_t i = 0; i < body_length; ++i)
                if (body[i] != '*' && body[i] != '^' && body[i] != '|')
                    ++useful;
            if (!useful || memchr(body, '|', body_length)) {
                free(options.domains);
                return YOZORA_FILTER_INVALID;
            }
        }
        for (size_t i = 0; i < body_length; ++i) {
            unsigned char c = (unsigned char)body[i];
            if (c <= 0x20 || c == 0x7f) {
                free(options.domains);
                return YOZORA_FILTER_INVALID;
            }
        }
        /* a site where nothing is blocked */
        if (exception && options.types_on == TYPE_BIT(YOZORA_FILTER_DOCUMENT) && !options.types_off && !options.third && !options.domains
            && rule.anchor_start == 2 && !rule.anchor_end && pattern_is_host(body, body_length)) {
            size_t host_length = body[body_length - 1] == '^' ? body_length - 1 : body_length;
            if (filter->site_count >= YOZORA_FILTER_MAX_RULES)
                return YOZORA_FILTER_FULL;
            return add_name(&filter->sites, &filter->site_count, &filter->site_capacity, body, host_length, YOZORA_FILTER_MAX_RULES)
                ? YOZORA_FILTER_ADDED : YOZORA_FILTER_NOMEM;
        }
        /* a rule that names only the document type cannot be matched against a subresource */
        if (options.types_on & TYPE_BIT(YOZORA_FILTER_DOCUMENT) && !exception) {
            free(options.domains);
            return YOZORA_FILTER_UNSUPPORTED;
        }
        if (filter->rule_count + filter->host_count >= YOZORA_FILTER_MAX_RULES) {
            free(options.domains);
            return YOZORA_FILTER_FULL;
        }
        if (!exception && rule.anchor_start == 2 && !rule.anchor_end && !options.types_on && !options.types_off && !options.third && !options.domains
            && pattern_is_host(body, body_length)) {
            size_t host_length = body[body_length - 1] == '^' ? body_length - 1 : body_length;
            filter->sorted = 0;
            return add_name(&filter->hosts, &filter->host_count, &filter->host_capacity, body, host_length, YOZORA_FILTER_MAX_RULES)
                ? YOZORA_FILTER_ADDED : YOZORA_FILTER_NOMEM;
        }
        if (!grow((void **)&filter->rules, &filter->rule_capacity, filter->rule_count, sizeof filter->rules[0], YOZORA_FILTER_MAX_RULES)) {
            free(options.domains);
            return YOZORA_FILTER_NOMEM;
        }
        rule.pattern = duplicate(body, body_length);
        if (!rule.pattern) {
            free(options.domains);
            return YOZORA_FILTER_NOMEM;
        }
        rule.length = (uint16_t)body_length;
        rule.exception = (uint8_t)exception;
        rule.third = options.third;
        rule.types_on = options.types_on;
        rule.types_off = options.types_off;
        rule.domains = options.domains;
        find_literal(&rule);
        filter->rules[filter->rule_count++] = rule;
    }
    return YOZORA_FILTER_ADDED;
}

static int selector_is_plain_css(const char *selector, size_t length)
{
    static const char *const procedural[] = {
        ":has-text(", ":contains(", ":xpath(", ":matches-css", ":-abp-", ":style(", ":remove(", ":upward(", ":nth-ancestor(", ":watch-attr(",
        ":min-text-length(", ":others(", ":matches-path(", ":matches-media(", ":matches-attr(", ":matches-property("
    };
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)selector[i];
        if (c < 0x20 || c == 0x7f || c == '{' || c == '}' || c == '<')
            return 0;
        if (c == '/' && i + 1 < length && selector[i + 1] == '*')
            return 0;
    }
    for (size_t i = 0; i < sizeof procedural / sizeof procedural[0]; ++i) {
        size_t n = strlen(procedural[i]);
        for (size_t at = 0; at + n <= length; ++at)
            if (!memcmp(selector + at, procedural[i], n))
                return 0;
    }
    return 1;
}

static yozora_filter_status add_hiding(yozora_filter *filter, const char *line, size_t length, size_t mark, int exception)
{
    hiding_rule rule;
    char *domains = NULL;
    const char *selector = line + mark + (exception ? 3 : 2);
    size_t selector_length = length - mark - (exception ? 3 : 2);
    int status;
    while (selector_length && (selector[0] == ' ' || selector[0] == '\t')) {
        ++selector;
        --selector_length;
    }
    if (!selector_length || selector_length > SELECTOR_MAX)
        return YOZORA_FILTER_INVALID;
    if (!selector_is_plain_css(selector, selector_length))
        return YOZORA_FILTER_UNSUPPORTED;
    /* the domains of a hiding rule are separated by commas, they are kept with a bar as the network options do */
    status = normalize_domains(line, mark, ',', &domains);
    if (status < 0)
        return YOZORA_FILTER_NOMEM;
    if (!status)
        return YOZORA_FILTER_INVALID;
    if (filter->hiding_count >= YOZORA_FILTER_MAX_HIDING) {
        free(domains);
        return YOZORA_FILTER_FULL;
    }
    if (!grow((void **)&filter->hiding, &filter->hiding_capacity, filter->hiding_count, sizeof filter->hiding[0], YOZORA_FILTER_MAX_HIDING)) {
        free(domains);
        return YOZORA_FILTER_NOMEM;
    }
    rule.selector = duplicate(selector, selector_length);
    if (!rule.selector) {
        free(domains);
        return YOZORA_FILTER_NOMEM;
    }
    rule.domains = domains;
    rule.exception = (uint8_t)exception;
    filter->hiding[filter->hiding_count++] = rule;
    return YOZORA_FILTER_ADDED;
}

yozora_filter_status yozora_filter_add_line(yozora_filter *filter, const char *line, size_t length)
{
    if (!filter || !line)
        return YOZORA_FILTER_INVALID;
    while (length && (line[length - 1] == '\n' || line[length - 1] == '\r' || line[length - 1] == ' ' || line[length - 1] == '\t'))
        --length;
    while (length && (*line == ' ' || *line == '\t')) {
        ++line;
        --length;
    }
    if (!length || line[0] == '!' || line[0] == '[')
        return YOZORA_FILTER_SKIPPED;
    if (length > YOZORA_FILTER_LINE_MAX)
        return YOZORA_FILTER_INVALID;
    if (memchr(line, '\0', length))
        return YOZORA_FILTER_INVALID;
    for (size_t i = 0; i + 1 < length; ++i) {
        if (line[i] != '#')
            continue;
        if (line[i + 1] == '#')
            return add_hiding(filter, line, length, i, 0);
        if (i + 2 < length && line[i + 1] == '@' && line[i + 2] == '#')
            return add_hiding(filter, line, length, i, 1);
        /* #?# #$# #%# and the like are extended syntax of other blockers */
        if (i + 2 < length && (line[i + 1] == '?' || line[i + 1] == '$' || line[i + 1] == '%' || line[i + 1] == '@') && line[i + 2] == '#')
            return YOZORA_FILTER_UNSUPPORTED;
    }
    return add_network(filter, line, length);
}

void yozora_filter_add_text(yozora_filter *filter, const char *text, size_t length, yozora_filter_report *report)
{
    size_t at = 0;
    yozora_filter_report local;
    if (!report)
        report = &local;
    memset(report, 0, sizeof *report);
    if (!filter || !text)
        return;
    while (at < length) {
        size_t end = at;
        while (end < length && text[end] != '\n')
            ++end;
        switch (yozora_filter_add_line(filter, text + at, end - at)) {
        case YOZORA_FILTER_ADDED: ++report->added; break;
        case YOZORA_FILTER_SKIPPED: ++report->skipped; break;
        case YOZORA_FILTER_UNSUPPORTED: ++report->unsupported; break;
        case YOZORA_FILTER_INVALID: ++report->invalid; break;
        case YOZORA_FILTER_FULL:
        case YOZORA_FILTER_NOMEM: ++report->dropped; break;
        }
        at = end + 1;
    }
    yozora_filter_finish(filter);
}

/* a separator is anything that is not a letter, a digit, or one of _ - . % */
static int is_separator(char c)
{
    return !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '%');
}

/* the end of the text the segment matches at pos, or -1. ^ matches a separator, or the end of the address without taking anything */
static long match_segment(const char *segment, size_t length, const char *text, size_t text_length, size_t pos)
{
    for (size_t i = 0; i < length; ++i) {
        if (segment[i] == '^') {
            if (pos == text_length)
                continue;
            if (!is_separator(text[pos]))
                return -1;
            ++pos;
        } else {
            if (pos >= text_length || text[pos] != segment[i])
                return -1;
            ++pos;
        }
    }
    return (long)pos;
}

/* the pattern from one start: the first segment there, the others at the earliest place, the last at the end
   when the rule is anchored there */
static int glob_from(const network_rule *rule, const char *text, size_t text_length, size_t start)
{
    size_t p = 0;
    size_t pos = start;
    int first = 1;
    for (;;) {
        size_t end = p;
        int last;
        long found = -1;
        while (end < rule->length && rule->pattern[end] != '*')
            ++end;
        last = end == rule->length;
        if (first) {
            found = match_segment(rule->pattern + p, end - p, text, text_length, pos);
            if (found < 0)
                return 0;
            if (last)
                return !rule->anchor_end || (size_t)found == text_length;
            pos = (size_t)found;
        } else if (end == p) {
            /* a run of stars, or a star at the end that takes the rest */
            if (last)
                return 1;
        } else {
            size_t q = pos;
            for (; q <= text_length; ++q) {
                found = match_segment(rule->pattern + p, end - p, text, text_length, q);
                if (found >= 0 && (!(last && rule->anchor_end) || (size_t)found == text_length))
                    break;
                found = -1;
            }
            if (found < 0)
                return 0;
            pos = (size_t)found;
            if (last)
                return 1;
        }
        first = 0;
        p = end + 1;
    }
}

/* the table holds the place of every three letters of the address, so most rules are turned away without a look at the text */
static int literal_in(const network_rule *rule, const char *text, size_t text_length, const uint8_t *seen)
{
    const char *needle = rule->pattern + rule->literal_offset;
    size_t n = rule->literal_length;
    if (n < 3)
        return 1;
    if (n > text_length || !(seen[rule->trigram >> 3] & (1u << (rule->trigram & 7))))
        return 0;
    for (size_t i = 0; i + n <= text_length; ++i)
        if (text[i] == needle[0] && !memcmp(text + i, needle, n))
            return 1;
    return 0;
}

static int rule_matches(const network_rule *rule, const char *text, size_t text_length, size_t host_start, size_t host_end, const uint8_t *seen)
{
    if (!literal_in(rule, text, text_length, seen))
        return 0;
    if (rule->anchor_start == 1)
        return glob_from(rule, text, text_length, 0);
    if (rule->anchor_start == 2) {
        for (size_t s = host_start; s < host_end; ++s) {
            if (s != host_start && text[s - 1] != '.')
                continue;
            if (glob_from(rule, text, text_length, s))
                return 1;
        }
        return 0;
    }
    /* the pattern of a rule that starts with a star is tried at one place, the star takes what the others would */
    if (rule->length && rule->pattern[0] == '*')
        return glob_from(rule, text, text_length, 0);
    {
        int single = !memchr(rule->pattern, '*', rule->length);
        for (size_t s = 0; s <= text_length; ++s) {
            if (glob_from(rule, text, text_length, s))
                return 1;
            /* the earliest place of the first segment is the best one, unless the end must be the end of the address */
            if (!(single && rule->anchor_end) && match_segment(rule->pattern, rule->length, text, text_length, s) >= 0)
                return 0;
        }
    }
    return 0;
}

static void host_span(const char *text, size_t length, size_t *start, size_t *end)
{
    size_t i = 0;
    size_t s = 0;
    size_t e;
    for (; i + 2 < length; ++i) {
        if (text[i] == ':' && text[i + 1] == '/' && text[i + 2] == '/') {
            s = i + 3;
            break;
        }
        if (text[i] == '/' || text[i] == '?' || text[i] == '#')
            break;
    }
    e = s;
    while (e < length && text[e] != '/' && text[e] != '?' && text[e] != '#')
        ++e;
    /* user information before the host */
    for (size_t k = s; k < e; ++k)
        if (text[k] == '@')
            s = k + 1;
    for (size_t k = s; k < e; ++k)
        if (text[k] == ':') {
            e = k;
            break;
        }
    *start = s;
    *end = e;
}

int yozora_filter_blocks(const yozora_filter *filter, const yozora_filter_request *request)
{
    char text[YOZORA_FILTER_URL_MAX + 1];
    uint8_t seen[8192];
    size_t length;
    size_t host_start;
    size_t host_end;
    uint16_t bit;
    int blocked = 0;
    if (!filter || !request || !request->url)
        return 0;
    length = request->url_length < YOZORA_FILTER_URL_MAX ? request->url_length : YOZORA_FILTER_URL_MAX;
    for (size_t i = 0; i < length; ++i) {
        text[i] = lower(request->url[i]);
        if (!text[i])
            return 0;
    }
    text[length] = '\0';
    memset(seen, 0, sizeof seen);
    for (size_t i = 0; i + 3 <= length; ++i) {
        uint16_t place = trigram_of(text + i);
        seen[place >> 3] = (uint8_t)(seen[place >> 3] | (1u << (place & 7)));
    }
    if (request->page_host && request->page_host_length
        && host_listed(filter->sites, filter->site_count, 0, request->page_host, request->page_host_length))
        return 0;
    host_span(text, length, &host_start, &host_end);
    bit = TYPE_BIT(request->type);
    if (request->host && request->host_length && yozora_blocklist_matches(request->host, request->host_length))
        blocked = 1;
    if (!blocked && request->host && request->host_length
        && host_listed(filter->hosts, filter->host_count, filter->sorted, request->host, request->host_length))
        blocked = 1;
    for (size_t i = 0; i < filter->rule_count; ++i) {
        const network_rule *rule = &filter->rules[i];
        uint16_t mask;
        if (blocked && !rule->exception)
            continue;
        if (!blocked && rule->exception)
            continue;
        mask = rule->types_on ? rule->types_on : (uint16_t)(ALL_TYPES & ~TYPE_BIT(YOZORA_FILTER_DOCUMENT));
        mask = (uint16_t)(mask & ~rule->types_off);
        if (!(mask & bit))
            continue;
        if (rule->third == 1 && !request->third_party)
            continue;
        if (rule->third == 2 && request->third_party)
            continue;
        if (rule->domains && !domains_allow(rule->domains, request->page_host ? request->page_host : "", request->page_host ? request->page_host_length : 0))
            continue;
        if (!rule_matches(rule, text, length, host_start, host_end, seen))
            continue;
        if (rule->exception)
            return 0;
        blocked = 1;
    }
    return blocked;
}

static int hiding_applies(const hiding_rule *rule, const char *host, size_t host_length)
{
    return !rule->domains || domains_allow(rule->domains, host, host_length);
}

size_t yozora_filter_hiding_css(const yozora_filter *filter, const char *host, size_t host_length, char *out, size_t capacity)
{
    static const char tail[] = "{display:none!important}\n";
    size_t used = 0;
    if (!out || !capacity)
        return 0;
    out[0] = '\0';
    if (!filter || !host || !host_length)
        return 0;
    if (host_listed(filter->sites, filter->site_count, 0, host, host_length))
        return 0;
    for (size_t i = 0; i < filter->hiding_count; ++i) {
        const hiding_rule *rule = &filter->hiding[i];
        size_t selector_length;
        int excepted = 0;
        if (rule->exception || !hiding_applies(rule, host, host_length))
            continue;
        for (size_t j = 0; j < filter->hiding_count && !excepted; ++j) {
            const hiding_rule *other = &filter->hiding[j];
            if (other->exception && !strcmp(other->selector, rule->selector) && hiding_applies(other, host, host_length))
                excepted = 1;
        }
        if (excepted)
            continue;
        selector_length = strlen(rule->selector);
        if (used + selector_length + sizeof tail > capacity)
            break;
        memcpy(out + used, rule->selector, selector_length);
        memcpy(out + used + selector_length, tail, sizeof tail);
        used += selector_length + sizeof tail - 1;
    }
    return used;
}

static const char *const builtin_lines[] = {
    /* the stat and ad calls that ride on hosts a site needs for its pages */
    "||youtube.com/api/stats/ads",
    "||youtube.com/pagead/",
    "||youtube.com/ptracking",
    "||youtube.com/get_midroll_info",
    "||connect.facebook.net/*/fbevents.js",
    "||facebook.com/tr/$third-party",
    "||facebook.com/tr?$third-party",
    "||vk.com/rtrg",
    "/pagead/conversion/",
    "/pagead/viewthroughconversion/",
    "/adsbygoogle.js",
    /* the markers of ad slots that the big networks and the Russian ones put on a page */
    "##ins.adsbygoogle",
    "##.adsbygoogle",
    "##iframe[id^=\"google_ads_iframe\"]",
    "##div[id^=\"google_ads_iframe\"]",
    "##div[id^=\"div-gpt-ad\"]",
    "##div[id^=\"yandex_rtb_\"]",
    "##div[id^=\"adfox_\"]",
    "##a[href^=\"https://ad.doubleclick.net/\"]",
    "##a[href*=\"//googleads.g.doubleclick.net/\"]",
};

int yozora_filter_add_builtin(yozora_filter *filter)
{
    int failed = 0;
    for (size_t i = 0; i < sizeof builtin_lines / sizeof builtin_lines[0]; ++i)
        if (yozora_filter_add_line(filter, builtin_lines[i], strlen(builtin_lines[i])) != YOZORA_FILTER_ADDED)
            failed = 1;
    return failed ? -1 : 0;
}
