#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <zlib.h>
#include <png.h>
#include <jpeglib.h>
#include <webp/decode.h>
#include <webp/encode.h>
#include <brotli/decode.h>
#include <brotli/encode.h>
#include <libxml/parser.h>
#include <sqlite3.h>
#include <libpsl.h>
#include <nghttp2/nghttp2.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <curl/curl.h>

#define CHECK(value) do { if (!(value)) { fprintf(stderr, "web dependency check failed: %s:%d: %s\n", __FILE__, __LINE__, #value); return 1; } } while (0)

static jmp_buf jpeg_failure;
static void jpeg_error(j_common_ptr common) {
    (*common->err->output_message)(common);
    longjmp(jpeg_failure, 1);
}

static int check_images(void) {
    unsigned char pixels[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    unsigned char encoded[2048], decoded[64];
    png_alloc_size_t length = sizeof(encoded);
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    image.width = image.height = 2;
    image.format = PNG_FORMAT_RGBA;
    CHECK(png_image_write_to_memory(&image, encoded, &length, 0, pixels, 0, NULL));
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    CHECK(png_image_begin_read_from_memory(&image, encoded, length));
    image.format = PNG_FORMAT_RGBA;
    CHECK(image.width == 2 && image.height == 2);
    CHECK(png_image_finish_read(&image, NULL, decoded, 0, NULL));
    CHECK(!memcmp(pixels, decoded, 16));
    png_image_free(&image);
    image.version = PNG_IMAGE_VERSION;
    CHECK(!png_image_begin_read_from_memory(&image, encoded, 7));
    png_image_free(&image);
    uint8_t *webp = NULL;
    size_t webp_size = WebPEncodeLosslessRGBA(pixels, 2, 2, 8, &webp);
    CHECK(webp_size && webp);
    CHECK(WebPDecodeRGBAInto(webp, webp_size, decoded, sizeof(decoded), 8));
    CHECK(!memcmp(pixels, decoded, 16));
    CHECK(!WebPDecodeRGBAInto(webp, 4, decoded, sizeof(decoded), 8));
    WebPFree(webp);
    struct jpeg_compress_struct compressor;
    struct jpeg_decompress_struct decompressor;
    struct jpeg_error_mgr error;
    unsigned char *jpeg = NULL;
    unsigned long jpeg_size = 0;
    if (setjmp(jpeg_failure))
        return 1;
    compressor.err = jpeg_std_error(&error);
    error.error_exit = jpeg_error;
    jpeg_create_compress(&compressor);
    jpeg_mem_dest(&compressor, &jpeg, &jpeg_size);
    compressor.image_width = compressor.image_height = 2;
    compressor.input_components = 3;
    compressor.in_color_space = JCS_RGB;
    jpeg_set_defaults(&compressor);
    jpeg_start_compress(&compressor, TRUE);
    unsigned char row[] = {255, 0, 0, 0, 255, 0};
    while (compressor.next_scanline < compressor.image_height) {
        JSAMPROW rows[] = {row};
        CHECK(jpeg_write_scanlines(&compressor, rows, 1) == 1);
    }
    jpeg_finish_compress(&compressor);
    jpeg_destroy_compress(&compressor);
    decompressor.err = &error;
    jpeg_create_decompress(&decompressor);
    jpeg_mem_src(&decompressor, jpeg, jpeg_size);
    CHECK(jpeg_read_header(&decompressor, TRUE) == JPEG_HEADER_OK);
    CHECK(jpeg_start_decompress(&decompressor));
    CHECK(decompressor.output_width == 2 && decompressor.output_height == 2);
    while (decompressor.output_scanline < decompressor.output_height) {
        JSAMPROW rows[] = {decoded};
        CHECK(jpeg_read_scanlines(&decompressor, rows, 1) == 1);
    }
    CHECK(jpeg_finish_decompress(&decompressor));
    jpeg_destroy_decompress(&decompressor);
    free(jpeg);
    return 0;
}

static size_t body_size;
static size_t receive_body(char *data, size_t size, size_t count, void *context) {
    (void)data;
    (void)context;
    if (size && count > (65536 - body_size) / size)
        return 0;
    body_size += size * count;
    return size * count;
}

int main(int argc, char **argv) {
    CHECK(argc == 1 || argc == 3);
    CHECK(!check_images());
    const unsigned char text[] = "yozora compression and decoding";
    unsigned char encoded[256], decoded[256];
    uLongf encoded_size = sizeof(encoded), decoded_size = sizeof(decoded);
    CHECK(compress2(encoded, &encoded_size, text, sizeof(text), 6) == Z_OK);
    CHECK(uncompress(decoded, &decoded_size, encoded, encoded_size) == Z_OK);
    CHECK(decoded_size == sizeof(text) && !memcmp(decoded, text, sizeof(text)));
    decoded_size = sizeof(decoded);
    CHECK(uncompress(decoded, &decoded_size, encoded, encoded_size / 2) != Z_OK);
    size_t brotli_size = sizeof(encoded), output_size = sizeof(decoded);
    CHECK(BrotliEncoderCompress(5, 20, BROTLI_MODE_GENERIC, sizeof(text), text, &brotli_size, encoded));
    CHECK(BrotliDecoderDecompress(brotli_size, encoded, &output_size, decoded) == BROTLI_DECODER_RESULT_SUCCESS);
    CHECK(output_size == sizeof(text) && !memcmp(decoded, text, sizeof(text)));
    xmlDocPtr document = xmlReadMemory("<page>ok</page>", 15, "fixture.xml", NULL, XML_PARSE_NONET);
    CHECK(document && !strcmp((const char *)xmlDocGetRootElement(document)->name, "page"));
    xmlFreeDoc(document);
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    CHECK(sqlite3_open(":memory:", &database) == SQLITE_OK);
    CHECK(sqlite3_prepare_v2(database, "select 42", -1, &statement, NULL) == SQLITE_OK);
    CHECK(sqlite3_stmt_readonly(statement) && !sqlite3_stmt_busy(statement));
    CHECK(sqlite3_step(statement) == SQLITE_ROW && sqlite3_column_int(statement, 0) == 42);
    CHECK(sqlite3_stmt_busy(statement));
    CHECK(sqlite3_finalize(statement) == SQLITE_OK);
    CHECK(sqlite3_db_release_memory(database) == SQLITE_OK);
    CHECK(sqlite3_close(database) == SQLITE_OK);
    CHECK(psl_builtin() && psl_is_public_suffix(psl_builtin(), "co.uk"));
    CHECK(!strcmp(psl_registrable_domain(psl_builtin(), "a.example.co.uk"), "example.co.uk"));
    unsigned char random[32], digest[EVP_MAX_MD_SIZE];
    unsigned int digest_size = 0;
    CHECK(RAND_bytes(random, sizeof(random)) == 1);
    CHECK(EVP_Digest("abc", 3, digest, &digest_size, EVP_sha256(), NULL));
    static const unsigned char expected[] = {0xba, 0x78, 0x16, 0xbf};
    CHECK(digest_size == 32 && !memcmp(digest, expected, sizeof(expected)));
    nghttp2_hd_deflater *deflater = NULL;
    CHECK(!nghttp2_hd_deflate_new(&deflater, 4096));
    nghttp2_nv header = {(uint8_t *)":method", (uint8_t *)"GET", 7, 3, NGHTTP2_NV_FLAG_NONE};
    CHECK(nghttp2_hd_deflate_hd(deflater, encoded, sizeof(encoded), &header, 1) > 0);
    nghttp2_hd_deflate_del(deflater);
    CHECK(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK);
    const curl_version_info_data *version = curl_version_info(CURLVERSION_NOW);
    CHECK(version->features & CURL_VERSION_SSL);
    CHECK(version->features & CURL_VERSION_HTTP2);
    CHECK(version->features & CURL_VERSION_BROTLI);
    CHECK(version->features & CURL_VERSION_PSL);
    if (argc == 3) {
        CURL *request = curl_easy_init();
        CHECK(request);
        CHECK(curl_easy_setopt(request, CURLOPT_URL, argv[1]) == CURLE_OK);
        CHECK(curl_easy_setopt(request, CURLOPT_CAINFO, argv[2]) == CURLE_OK);
        CHECK(curl_easy_setopt(request, CURLOPT_PROTOCOLS_STR, "https") == CURLE_OK);
        CHECK(curl_easy_setopt(request, CURLOPT_NOPROXY, "*") == CURLE_OK);
        CHECK(curl_easy_setopt(request, CURLOPT_TIMEOUT, 5L) == CURLE_OK);
        CHECK(curl_easy_setopt(request, CURLOPT_WRITEFUNCTION, receive_body) == CURLE_OK);
        CURLcode result = curl_easy_perform(request);
        curl_easy_cleanup(request);
        curl_global_cleanup();
        printf("tls-result:%d bytes:%zu\n", (int)result, body_size);
        return (int)result;
    }
    curl_global_cleanup();
    puts("web dependencies: images, compression, XML, SQLite, PSL, entropy, SHA-256 and HTTP/2 passed");
    return 0;
}
