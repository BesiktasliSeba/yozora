#include "yozora_translate.h"
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "test_translate: %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

static void detects(const char *text, const char *declared, const char *expected)
{
    char code[16];
    int confidence = yozora_tr_detect(text, strlen(text), declared, code, sizeof code);
    if (strcmp(code, expected)) {
        fprintf(stderr, "test_translate: detected \"%s\" (%d) for a text of \"%s\", expected \"%s\"\n", code, confidence, expected, expected);
        ++failures;
    }
}

int main(void)
{
    /* the table and the codes */
    CHECK(yozora_tr_language_count() > 50);
    CHECK(yozora_tr_language_find("ru") >= 0 && !strcmp(yozora_tr_language_name((size_t)yozora_tr_language_find("ru")), "Русский"));
    CHECK(yozora_tr_language_find("FR-ca") == yozora_tr_language_find("fr"));
    CHECK(yozora_tr_language_find("pt_BR") == yozora_tr_language_find("pt"));
    CHECK(yozora_tr_language_find("zh-Hans") == yozora_tr_language_find("zh-CN"));
    CHECK(yozora_tr_language_find("zh-HK") == yozora_tr_language_find("zh-TW"));
    CHECK(yozora_tr_language_find("iw") == yozora_tr_language_find("he"));
    CHECK(yozora_tr_language_find("nb") == yozora_tr_language_find("no"));
    CHECK(yozora_tr_language_find("xx") == -1 && yozora_tr_language_find("") == -1 && yozora_tr_language_find(NULL) == -1);

    /* the language of a text */
    detects("The quick brown fox jumps over the lazy dog and it is not the end of this sentence for you to read", NULL, "en");
    detects("Der schnelle braune Fuchs springt über den faulen Hund und das ist nicht das Ende von diesem Satz", NULL, "de");
    detects("Le renard brun rapide saute par dessus le chien paresseux et ce n'est pas la fin de la phrase pour vous", NULL, "fr");
    detects("El rápido zorro marrón salta sobre el perro perezoso y no es el final de la frase para que lo leas", NULL, "es");
    detects("Il veloce volpe marrone salta sopra il cane pigro e non è la fine della frase per voi che leggete", NULL, "it");
    detects("A rápida raposa marrom pula sobre o cão preguiçoso e não é o fim da frase para você que lê", NULL, "pt");
    detects("De snelle bruine vos springt over de luie hond en dat is niet het einde van deze zin voor jou", NULL, "nl");
    detects("Szybki brązowy lis skacze przez leniwego psa i to nie jest koniec tego zdania dla ciebie który czytasz", NULL, "pl");
    detects("Bu hızlı kahverengi tilki tembel köpeğin üzerinden atlar ve bu cümlenin sonu değil için çok daha", NULL, "tr");
    detects("Быстрая коричневая лиса прыгает через ленивую собаку, и это ещё не конец этого предложения", NULL, "ru");
    detects("Швидка руда лисиця стрибає через лінивого собаку, і це ще не кінець цього речення для вас", NULL, "uk");
    detects("Бърза кафява лисица скача над мързеливото куче и това не е краят на изречението", NULL, "bg");
    detects("Γρήγορη καφέ αλεπού πηδά πάνω από τον τεμπέλη σκύλο και αυτό δεν είναι το τέλος", NULL, "el");
    detects("שועל חום מהיר קופץ מעל הכלב העצלן וזה לא סוף המשפט הזה בשבילכם", NULL, "he");
    detects("الثعلب البني السريع يقفز فوق الكلب الكسول وهذه ليست نهاية هذه الجملة بالنسبة لك", NULL, "ar");
    detects("روباه قهوه ای تند از روی سگ تنبل می پرد و این پایان این جمله برای شما نیست", NULL, "fa");
    detects("素早い茶色の狐がのろまな犬を飛び越える。そしてこれはこの文の終わりではありません", NULL, "ja");
    detects("敏捷的棕色狐狸跳过了懒惰的狗，这不是这个句子的结尾，请继续阅读下去", NULL, "zh-CN");
    detects("敏捷的棕色狐狸跳過了懶惰的狗，這不是這個句子的結尾，我們請你繼續閱讀下去", NULL, "zh-TW");
    detects("빠른 갈색 여우가 게으른 개를 뛰어넘습니다 그리고 이것은 이 문장의 끝이 아닙니다", NULL, "ko");
    detects("เร็วสีน้ำตาลจิ้งจอกกระโดดข้ามสุนัขขี้เกียจและนี่ไม่ใช่จุดสิ้นสุดของประโยคนี้", NULL, "th");
    detects("Con cáo nâu nhanh nhẹn nhảy qua con chó lười biếng và đây không phải là phần kết của câu này", NULL, "vi");
    /* too little to tell, digits and symbols only */
    detects("ok", NULL, "");
    detects("12345 67890 !!! ???", NULL, "");
    detects("", NULL, "");
    /* a page that says it is English and is Russian is Russian */
    detects("Быстрая коричневая лиса прыгает через ленивую собаку, и это ещё не конец этого предложения", "en", "ru");

    /* the request */
    char url[256];
    CHECK(yozora_tr_url("auto", "ru", url, sizeof url) > 0);
    CHECK(!strcmp(url, "https://translate.googleapis.com/translate_a/t?client=gtx&format=html&v=1.0&sl=auto&tl=ru"));
    CHECK(yozora_tr_url("auto", "zh-CN", url, sizeof url) > 0 && strstr(url, "tl=zh-CN"));
    CHECK(yozora_tr_url("auto", "ru", url, 40) == 0);
    const char *texts[] = { "Hello", "a < b & c > d", "Привет мир" };
    char body[512];
    size_t size = yozora_tr_body(texts, 0, 3, body, sizeof body);
    CHECK(size == strlen(body));
    CHECK(!strncmp(body, "q=%3Ca%20i%3D0%3EHello%3C%2Fa%3E", strlen("q=%3Ca%20i%3D0%3EHello%3C%2Fa%3E")));
    CHECK(strstr(body, "a%20%26lt%3B%20b%20%26amp%3B%20c%20%26gt%3B%20d"));
    CHECK(strstr(body, "%3Ca%20i%3D2%3E%D0%9F"));
    /* a batch from the middle counts its items from zero */
    size = yozora_tr_body(texts, 1, 2, body, sizeof body);
    CHECK(size > 0 && strstr(body, "i%3D0%3Ea%20%26lt") && strstr(body, "i%3D1%3E%D0%9F"));
    CHECK(yozora_tr_body(texts, 0, 3, body, 20) == 0);
    CHECK(yozora_tr_body(texts, 0, 3, body, 2) == 0);

    /* batches */
    size_t lengths[] = { 100, 100, 100, 5000, 10, 10 };
    CHECK(yozora_tr_batch(lengths, 6, 0, 1000, 50) == 3);
    CHECK(yozora_tr_batch(lengths, 6, 0, 1000, 2) == 2);
    CHECK(yozora_tr_batch(lengths, 6, 3, 1000, 50) == 1);
    CHECK(yozora_tr_batch(lengths, 6, 4, 1000, 50) == 2);
    CHECK(yozora_tr_batch(lengths, 6, 6, 1000, 50) == 0);
    CHECK(yozora_tr_batch(lengths, 6, 0, 1000, 0) == 0);

    /* an answer of the service, as it came */
    const char *items[3];
    char arena[512], detected[16];
    const char answer[] = "[[\"<a i=0>Hallo,</a> <a i=1>wie geht es dir</a>\",\"en\"]]";
    CHECK(yozora_tr_parse(answer, strlen(answer), 2, arena, sizeof arena, items, detected, sizeof detected) == YOZORA_TR_OK);
    CHECK(!strcmp(items[0], "Hallo,") && !strcmp(items[1], "wie geht es dir") && !strcmp(detected, "en"));
    /* escapes of json and of html, and a tag the service added inside an item */
    const char escaped[] = "[[\"<a i=0>\\u041f\\u0440\\u0438\\u0432\\u0435\\u0442 &amp; &lt;\\u0434\\u0440\\u0443\\u0433&gt;</a><a i=1>\\\"<b>x</b>\\\" &#39;y&#39; &#x41; \\ud83d\\ude00\\n</a>\",\"ru\"]]";
    CHECK(yozora_tr_parse(escaped, strlen(escaped), 2, arena, sizeof arena, items, detected, sizeof detected) == YOZORA_TR_OK);
    CHECK(!strcmp(items[0], "Привет & <друг>"));
    CHECK(!strcmp(items[1], "\"x\" 'y' A \xF0\x9F\x98\x80\n"));
    /* items out of order, one missing, one twice and one past the end */
    const char partial[] = "[[\"<a i=2>c</a> <a i=0>a</a> <a i=0>again</a> <a i=7>far</a>\",\"en\"]]";
    CHECK(yozora_tr_parse(partial, strlen(partial), 3, arena, sizeof arena, items, detected, sizeof detected) == YOZORA_TR_PARTIAL);
    CHECK(!strcmp(items[0], "a") && items[1] == NULL && !strcmp(items[2], "c"));
    /* a quote around the number */
    const char spaced[] = "[[\"<a i=\\\"0\\\">x</a>\",\"en\"]]";
    CHECK(yozora_tr_parse(spaced, strlen(spaced), 1, arena, sizeof arena, items, detected, sizeof detected) == YOZORA_TR_OK && !strcmp(items[0], "x"));
    /* more than one pair: the text of the pairs goes one after the other */
    const char pairs[] = "[[\"<a i=0>one</a>\",\"en\"],[\"<a i=1>two</a>\",\"en\"]]";
    CHECK(yozora_tr_parse(pairs, strlen(pairs), 2, arena, sizeof arena, items, detected, sizeof detected) == YOZORA_TR_OK);
    CHECK(!strcmp(items[0], "one") && !strcmp(items[1], "two"));
    /* what is not an answer */
    const char blocked[] = "<html><head><title>Sorry...</title></head><body>unusual traffic</body></html>";
    CHECK(yozora_tr_parse(blocked, strlen(blocked), 1, arena, sizeof arena, items, detected, sizeof detected) == YOZORA_TR_BLOCKED);
    const char page[] = "<html>captive portal</html>";
    CHECK(yozora_tr_parse(page, strlen(page), 1, arena, sizeof arena, items, detected, sizeof detected) == YOZORA_TR_MALFORMED);
    const char cut[] = "[[\"<a i=0>half";
    CHECK(yozora_tr_parse(cut, strlen(cut), 1, arena, sizeof arena, items, detected, sizeof detected) == YOZORA_TR_MALFORMED);
    CHECK(yozora_tr_parse("", 0, 1, arena, sizeof arena, items, detected, sizeof detected) == YOZORA_TR_MALFORMED);
    CHECK(yozora_tr_parse("[]", 2, 1, arena, sizeof arena, items, detected, sizeof detected) == YOZORA_TR_MALFORMED);
    CHECK(yozora_tr_parse("[[\"no items\",\"en\"]]", 19, 1, arena, sizeof arena, items, detected, sizeof detected) == YOZORA_TR_MALFORMED);
    /* an answer larger than the arena */
    char small[16];
    CHECK(yozora_tr_parse(answer, strlen(answer), 2, small, sizeof small, items, detected, sizeof detected) == YOZORA_TR_TOO_LONG);

    if (failures)
        return 1;
    puts("translate: languages, detection, request, batches and answers passed");
    return 0;
}
