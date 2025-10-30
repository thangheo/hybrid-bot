#include "risk/alerting/webhook_client.hpp"
#include <iostream>

#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#endif

namespace trading::risk::alerting {

WebhookClient::WebhookClient(const std::vector<std::string>& endpoints) : endpoints_(endpoints) {
#ifdef HAVE_LIBCURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
#endif
}

WebhookClient::~WebhookClient() {
#ifdef HAVE_LIBCURL
    curl_global_cleanup();
#endif
}

bool WebhookClient::send(const std::string& payload) {
    bool anySuccess = false;
    for (const auto& url : endpoints_) {
#ifdef HAVE_LIBCURL
        CURL* curl = curl_easy_init();
        if (!curl) continue;

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) anySuccess = true;

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
#else
        // Fallback: log payload to console
        std::cerr << "Webhook fallback - POST to " << url << ": " << payload << std::endl;
        anySuccess = true;
#endif
    }
    return anySuccess;
}

} // namespace trading::risk::alerting
