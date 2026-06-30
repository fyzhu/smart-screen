/*
 * @Author: xiaozhi
 * @Date: 2024-09-30 00:21:03
 * @Last Modified by: xiaozhi
 * @Last Modified time: 2024-10-08 23:45:16
 */

#include <stdio.h>
#include <stdlib.h>
#include "cJSON/cJSON.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#include "lvgl.h"
#include "http_manager.h"
#include "osal_thread.h"
#include "osal_queue.h"

#include <time.h>
#include <zlib.h>

#ifndef HTTP_HAS_OPENSSL
#define HTTP_HAS_OPENSSL 0
#endif

#if HTTP_HAS_OPENSSL
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/bn.h>
#include <openssl/sha.h>
#include <openssl/err.h>
#endif

static osal_queue_t net_queue = NULL;
static osal_thread_t net_thread = NULL;
static weather_callback_fun weather_callback_func = NULL;
static air_callback_fun air_callback_func = NULL;
static char *jwt_token = NULL;
static time_t jwt_token_exp = 0;

static void weather_callback_dispatch(void *user_data)
{
    char *weather_info = (char *)user_data;
    if (weather_info != NULL && weather_callback_func != NULL)
    {
        weather_callback_func(weather_info);
    }
    free(weather_info);
}

static void air_callback_dispatch(void *user_data)
{
    char *air_info = (char *)user_data;
    if (air_info != NULL && air_callback_func != NULL)
    {
        air_callback_func(air_info);
    }
    free(air_info);
}

#if HTTP_HAS_OPENSSL
static const char *jwt_private_key_b64 = "MC4CAQAwBQYDK2VwBCIEIA2ecC7MRch9flC1IpRchTBSZ3AeAYJ+/VqvIS+IffpB";
static const char *jwt_credential_id = "CCGX3U7PR2";
static const char *jwt_project_id = "34TQHVEXMR";

static char *base64url_encode(const unsigned char *input, size_t len)
{
    BIO *b64 = BIO_new(BIO_f_base64());
    BIO *bmem = BIO_new(BIO_s_mem());
    if (!b64 || !bmem)
    {
        BIO_free_all(b64);
        BIO_free_all(bmem);
        return NULL;
    }
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    b64 = BIO_push(b64, bmem);
    BIO_write(b64, input, (int)len);
    BIO_flush(b64);
    BUF_MEM *bptr = NULL;
    BIO_get_mem_ptr(b64, &bptr);
    if (!bptr)
    {
        BIO_free_all(b64);
        return NULL;
    }

    char *output = malloc(bptr->length + 1);
    if (!output)
    {
        BIO_free_all(b64);
        return NULL;
    }
    memcpy(output, bptr->data, bptr->length);
    output[bptr->length] = '\0';
    BIO_free_all(b64);

    size_t out_len = strlen(output);
    for (size_t i = 0; i < out_len; i++)
    {
        if (output[i] == '+')
            output[i] = '-';
        else if (output[i] == '/')
            output[i] = '_';
    }
    while (out_len > 0 && output[out_len - 1] == '=')
        output[--out_len] = '\0';

    return output;
}

static unsigned char *base64url_decode(const char *input, size_t *out_len)
{
    size_t len = strlen(input);
    size_t pad = (4 - len % 4) % 4;
    char *temp = malloc(len + pad + 1);
    if (!temp)
        return NULL;
    for (size_t i = 0; i < len; i++)
    {
        if (input[i] == '-')
            temp[i] = '+';
        else if (input[i] == '_')
            temp[i] = '/';
        else
            temp[i] = input[i];
    }
    for (size_t i = 0; i < pad; i++)
        temp[len + i] = '=';
    temp[len + pad] = '\0';

    BIO *b64 = BIO_new(BIO_f_base64());
    BIO *bmem = BIO_new_mem_buf(temp, -1);
    if (!b64 || !bmem)
    {
        BIO_free_all(b64);
        BIO_free_all(bmem);
        free(temp);
        return NULL;
    }
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    b64 = BIO_push(b64, bmem);
    unsigned char *buffer = malloc(len + pad);
    if (!buffer)
    {
        BIO_free_all(b64);
        free(temp);
        return NULL;
    }
    int decoded_len = BIO_read(b64, buffer, (int)(len + pad));
    BIO_free_all(b64);
    free(temp);
    if (decoded_len <= 0)
    {
        free(buffer);
        return NULL;
    }
    *out_len = (size_t)decoded_len;
    return buffer;
}

static EVP_PKEY *load_ed25519_private_key(const char *key_b64)
{
    size_t der_len = 0;
    unsigned char *der = base64url_decode(key_b64, &der_len);
    if (!der)
        return NULL;

    const unsigned char *p = der;
    EVP_PKEY *pkey = d2i_AutoPrivateKey(NULL, &p, (long)der_len);
    free(der);
    return pkey;
}

static int generate_jwt_token(void)
{
    time_t now = time(NULL);
    if (now == ((time_t)-1))
        return -1;

    if (jwt_token && now + 60 < jwt_token_exp)
        return 0;

    EVP_PKEY *pkey = load_ed25519_private_key(jwt_private_key_b64);
    if (!pkey)
        return -1;

    time_t iat = now - 30;
    time_t exp = iat + 86400;

    char header[128];
    char payload[256];
    snprintf(header, sizeof(header), "{\"alg\":\"EdDSA\",\"kid\":\"%s\"}", jwt_credential_id);
    snprintf(payload, sizeof(payload), "{\"sub\":\"%s\",\"iat\":%lld,\"exp\":%lld}", jwt_project_id, (long long)iat, (long long)exp);

    char *header_b64 = base64url_encode((unsigned char *)header, strlen(header));
    char *payload_b64 = base64url_encode((unsigned char *)payload, strlen(payload));
    if (!header_b64 || !payload_b64)
    {
        EVP_PKEY_free(pkey);
        free(header_b64);
        free(payload_b64);
        return -1;
    }

    size_t signing_input_len = strlen(header_b64) + 1 + strlen(payload_b64);
    char *signing_input = malloc(signing_input_len + 1);
    if (!signing_input)
    {
        EVP_PKEY_free(pkey);
        free(header_b64);
        free(payload_b64);
        return -1;
    }
    snprintf(signing_input, signing_input_len + 1, "%s.%s", header_b64, payload_b64);

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx)
    {
        EVP_PKEY_free(pkey);
        free(header_b64);
        free(payload_b64);
        free(signing_input);
        return -1;
    }

    if (EVP_DigestSignInit(mdctx, NULL, NULL, NULL, pkey) != 1)
    {
        EVP_PKEY_free(pkey);
        EVP_MD_CTX_free(mdctx);
        free(header_b64);
        free(payload_b64);
        free(signing_input);
        return -1;
    }

    size_t siglen = 0;
    if (EVP_DigestSign(mdctx, NULL, &siglen, (unsigned char *)signing_input, strlen(signing_input)) != 1)
    {
        EVP_PKEY_free(pkey);
        EVP_MD_CTX_free(mdctx);
        free(header_b64);
        free(payload_b64);
        free(signing_input);
        return -1;
    }

    unsigned char *sig = malloc(siglen);
    if (!sig)
    {
        EVP_PKEY_free(pkey);
        EVP_MD_CTX_free(mdctx);
        free(header_b64);
        free(payload_b64);
        free(signing_input);
        return -1;
    }

    if (EVP_DigestSign(mdctx, sig, &siglen, (unsigned char *)signing_input, strlen(signing_input)) != 1)
    {
        free(sig);
        EVP_PKEY_free(pkey);
        EVP_MD_CTX_free(mdctx);
        free(header_b64);
        free(payload_b64);
        free(signing_input);
        return -1;
    }

    char *sig_b64 = base64url_encode(sig, siglen);
    free(sig);
    EVP_PKEY_free(pkey);
    EVP_MD_CTX_free(mdctx);
    free(signing_input);
    if (!sig_b64)
    {
        free(header_b64);
        free(payload_b64);
        return -1;
    }

    size_t token_len = strlen(header_b64) + 1 + strlen(payload_b64) + 1 + strlen(sig_b64);
    char *token = malloc(token_len + 1);
    if (!token)
    {
        free(header_b64);
        free(payload_b64);
        free(sig_b64);
        return -1;
    }
    snprintf(token, token_len + 1, "%s.%s.%s", header_b64, payload_b64, sig_b64);

    free(header_b64);
    free(payload_b64);
    free(sig_b64);

    free(jwt_token);
    jwt_token = token;
    jwt_token_exp = exp;
    return 0;
}
#else
static int generate_jwt_token(void)
{
    return -1;
}
#endif

static char *gunzip_decompress(const char *in, size_t in_len, size_t *out_len)
{
    if (in_len < 2 || (unsigned char)in[0] != 0x1f || (unsigned char)in[1] != 0x8b)
        return NULL;
    z_stream zs = {0};
    if (inflateInit2(&zs, 15 + 16) != Z_OK)
        return NULL;
    size_t buf_size = in_len * 6 + 1024;
    char *out = malloc(buf_size + 1);
    if (!out) { inflateEnd(&zs); return NULL; }
    zs.next_in  = (Bytef *)in;
    zs.avail_in = (uInt)in_len;
    zs.next_out = (Bytef *)out;
    zs.avail_out = (uInt)buf_size;
    int ret = inflate(&zs, Z_FINISH);
    if (ret != Z_STREAM_END) {
        free(out); inflateEnd(&zs); return NULL;
    }
    *out_len = zs.total_out;
    out[*out_len] = '\0';
    inflateEnd(&zs);
    return out;
}

static char *url_encode(CURL *curl, const char *str){
    if (!curl || !str)
        return NULL;
    return curl_easy_escape(curl, str, 0);
}

/**
 * @brief 组装HTTP请求URL
 */
static int assemble_url(const char *host, const char *path, char **url)
{
    *url = malloc(strlen(host) + strlen(path) + 1);
    strcpy(*url, host);
    strcat(*url, path);
    return 0;
}

/**
 * @brief CURL数据接收回调函数
 */
static size_t write_callback(void *data, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    http_resp_data_t *mem = (http_resp_data_t *)userp;

    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr)
        return 0; // 内存分配失败

    mem->data = ptr;
    memcpy(mem->data + mem->size, data, realsize);
    mem->size += realsize;
    mem->data[mem->size] = '\0';
    return realsize;
}

int http_request_method(const char *host, const char *path, const char *method, const char *request_json, char **response_json)
{
    CURL *curl = curl_easy_init();
    if (!curl)
        return -1;

    // 组装并设置URL
    char *url = NULL;
    assemble_url(host, path, &url);
    printf("Request path: %s\n", path);
    curl_easy_setopt(curl, CURLOPT_URL, url);

    // 通用配置
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);        // 调试模式：启用详细输出模式
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);       // 设置请求超时时间（单位：秒），20L表示超过20秒无响应则终止请求
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // 禁用SSL证书验证（0L表示关闭），跳过对服务器SSL证书的有效性检查
    // 设置响应处理
    http_resp_data_t response_data = {0};
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback); // 注册响应数据接收回调函数
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);     // 指定回调函数的用户数据

    // POST方法特殊处理
    if (strcmp(method, "POST") == 0)
    {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_json);
    }
    // 设置HTTP头部
    struct curl_slist *header = curl_slist_append(NULL, "Content-Type: application/json");
    if (generate_jwt_token() == 0 && jwt_token != NULL)
    {
        char auth_header[512];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", jwt_token);
        header = curl_slist_append(header, auth_header);
    }
    header = curl_slist_append(header, "Accept: application/json");
    header = curl_slist_append(header, "User-Agent: app_sdk/1.0");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header);
    // 执行请求
    printf("Request URL: %s\n", url);
    CURLcode code = curl_easy_perform(curl);
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    int ret = (code == CURLE_OK) ? 0 : -1;
    if (response_json == NULL)
    {
        return -1;
    }
    // 处理响应
    if (ret == 0)
    {
        // 检测并解压 gzip 响应
        if (response_data.size >= 2 &&
            (unsigned char)response_data.data[0] == 0x1f &&
            (unsigned char)response_data.data[1] == 0x8b)
        {
            size_t decompressed_len = 0;
            char *decompressed = gunzip_decompress(response_data.data, response_data.size, &decompressed_len);
            if (decompressed)
            {
                free(response_data.data);
                response_data.data = decompressed;
                response_data.size = decompressed_len;
            }
        }
        printf("HTTP %ld Response len: %ld\n", response_code, response_data.size);
        printf("Response body: %.2000s\n", response_data.data ? response_data.data : "(null)");
        *response_json = response_data.data; // 转移内存所有权
    }
    else
    {
        printf("Request failed: %s (%d), HTTP %ld\n", curl_easy_strerror(code), code, response_code);
        if (response_data.data)
        {
            printf("Response body: %.512s\n", response_data.data);
            free(response_data.data);
        }
    }
    // 资源清理
    curl_slist_free_all(header);
    free(url);
    curl_easy_cleanup(curl);
    return ret;
}

void parseWeatherData(const char *json_data)
{
    cJSON *root = cJSON_Parse(json_data);
    if (!root)
    {
        fprintf(stderr, "Error parsing JSON data.\n");
        fprintf(stderr, "Raw response: %.512s\n", json_data ? json_data : "(null)");
        return;
    }

    cJSON *code = cJSON_GetObjectItem(root, "code");
    if (!code || !cJSON_IsString(code) || strcmp(code->valuestring, "200") != 0)
    {
        fprintf(stderr, "Invalid QWeather response code.\n");
        cJSON_Delete(root);
        return;
    }

    cJSON *now = cJSON_GetObjectItem(root, "now");
    if (!now || !cJSON_IsObject(now))
    {
        fprintf(stderr, "Invalid JSON format: missing 'now' object.\n");
        cJSON_Delete(root);
        return;
    }

    cJSON *text = cJSON_GetObjectItem(now, "text");
    cJSON *temp = cJSON_GetObjectItem(now, "temp");
    if (!text || !cJSON_IsString(text) || !temp || !cJSON_IsString(temp))
    {
        fprintf(stderr, "Invalid JSON format: missing weather text or temperature.\n");
        cJSON_Delete(root);
        return;
    }

    char *weather_info = malloc(64);
    if (!weather_info)
    {
        cJSON_Delete(root);
        return;
    }
    snprintf(weather_info, 64, "%s %s°C", text->valuestring, temp->valuestring);
    if (weather_callback_func != NULL)
        lv_async_call(weather_callback_dispatch, weather_info);
    else
        free(weather_info);
    cJSON_Delete(root);
}

void parseAirData(const char *json_data)
{
    cJSON *root = cJSON_Parse(json_data);
    if (!root)
    {
        fprintf(stderr, "Error parsing JSON data.\n");
        fprintf(stderr, "Raw response: %.512s\n", json_data ? json_data : "(null)");
        return;
    }

    // cJSON *code = cJSON_GetObjectItem(root, "code");
    // if (!code || !cJSON_IsString(code) || strcmp(code->valuestring, "200") != 0)
    // {
    //     fprintf(stderr, "Invalid QWeather response code.\n");
    //     cJSON_Delete(root);
    //     return;
    // }

    cJSON *indexes = cJSON_GetObjectItem(root, "indexes");
    if (!indexes || !cJSON_IsArray(indexes))
    {
        fprintf(stderr, "Invalid JSON format: missing 'indexes' array.\n");
        cJSON_Delete(root);
        return;
    }
    cJSON *now = cJSON_GetArrayItem(indexes, 0);
    if (!now || !cJSON_IsObject(now))
    {
        fprintf(stderr, "Invalid JSON format: missing 'now' object in 'indexes'.\n");
        cJSON_Delete(root);
        return;
    }
    cJSON *category = cJSON_GetObjectItem(now, "category");
    cJSON *aqi = cJSON_GetObjectItem(now, "aqiDisplay");
    if (!category || !cJSON_IsString(category) || !aqi || !cJSON_IsString(aqi))
    {
        fprintf(stderr, "Invalid JSON format: missing air quality category or aqi.\n");
        cJSON_Delete(root);
        return;
    }

    char *air_info = malloc(64);
    if (!air_info)
    {
        cJSON_Delete(root);
        return;
    }
    snprintf(air_info, 64, "%s %s", category->valuestring, aqi->valuestring);
    if (air_callback_func != NULL)
        lv_async_call(air_callback_dispatch, air_info);
    else
        free(air_info);
    cJSON_Delete(root);
}

// 网络模块线程
static void *net_thread_fun(void *arg)
{
    int ret = OSAL_ERROR;
    net_obj obj;
    memset(&obj, 0, sizeof(net_obj));
    char *response_json_str = NULL;
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    while (1)
    {
        ret = osal_queue_recv(&net_queue, (void *)&obj, 100);
        if (ret == OSAL_SUCCESS)
        {
            NET_COMM_ID id = obj.id;
            switch (id)
            {
            case NET_GET_WEATHER:
                // printf("handle NET_GET_WEATHER\n");
                http_request_method(obj.host, obj.path, obj.type, obj.data, &response_json_str);
                if (response_json_str != NULL)
                {
                    parseWeatherData(response_json_str);
                    free(response_json_str);
                }
                break;

            case NET_GET_AIR:
                // printf("handle NET_GET_AIR\n");
                http_request_method(obj.host, obj.path, obj.type, obj.data, &response_json_str);
                if (response_json_str != NULL)
                {
                    parseAirData(response_json_str);
                    free(response_json_str);
                }
                break;
            default:
                break;
            }
        }
        osal_thread_sleep(500);
    }
}

// 异步获取天气
void http_get_weather_async(char *key, char *city)
{
    net_obj obj;
    memset(&obj, 0, sizeof(net_obj));
    strcpy(obj.host, "https://ny5ctwnev7.re.qweatherapi.com");

    CURL *curl = curl_easy_init();
    char *escaped_city = url_encode(curl, city);
    if (curl)
        curl_easy_cleanup(curl);

    if (escaped_city)
    {
        snprintf(obj.path, sizeof(obj.path), "/v7/weather/now?location=%s&lang=zh&unit=m", escaped_city);
        curl_free(escaped_city);
    }
    else
    {
        snprintf(obj.path, sizeof(obj.path), "/v7/weather/now?location=%s&lang=zh&unit=m", city);
    }

    obj.id = NET_GET_WEATHER;
    strcpy(obj.data, "");
    strcpy(obj.type, "GET");
    int ret = osal_queue_send(&net_queue, &obj, sizeof(net_obj), 1000);
    if (ret == OSAL_ERROR)
    {
        printf("queue send error");
    }
}
void http_get_air_async(char *key, char *latlon)
{
    net_obj obj;
    memset(&obj, 0, sizeof(net_obj));
    strcpy(obj.host, "https://ny5ctwnev7.re.qweatherapi.com");

    sprintf(obj.path, "/airquality/v1/current/%s", latlon);

    obj.id = NET_GET_AIR;
    strcpy(obj.data, "");
    strcpy(obj.type, "GET");
    int ret = osal_queue_send(&net_queue, &obj, sizeof(net_obj), 1000);
    if (ret == OSAL_ERROR)
    {
        printf("queue send error");
    }
}

// 设置获取天气回调函数
void http_set_weather_callback(weather_callback_fun func)
{
    weather_callback_func = func;
}
void http_set_air_callback(air_callback_fun func)
{
    air_callback_func = func;
}

// HTTP模块创建
int http_request_create()
{
    int ret = OSAL_ERROR;
    ret = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (ret != 0)
        return -1;
    ret = osal_queue_create(&net_queue, "net_queue", sizeof(net_obj), 50);
    if (ret == OSAL_ERROR)
    {
        printf("create queue error");
        return -1;
    }
    ret = osal_thread_create(&net_thread, net_thread_fun, NULL);
    if (ret == OSAL_ERROR)
    {
        printf("create thread error");
        return -1;
    }
    return 0;
}
