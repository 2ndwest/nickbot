#pragma once

#include <cpr/cpr.h>
#include <curl/curl.h>
#include <fstream>
#include <libtouchstone.h>
#include <sstream>
#include <string>
#include "config.h"

namespace tls {

// Since Nov 2026, classrooms.mit.edu serves its SSL.com certificate with the old InCommon chain instead of this
// intermediate. Secure Transport then fails with "SSL: peer not verified: RecoverableTrustFailure" wherever macOS
// can't download the missing intermediate itself (it doesn't remember one it downloaded earlier), so we trust it
// ourselves. Valid until Oct 2032; served by idp.mit.edu, or http://cert.ssl.com/SSL.com-TLS-I-RSA-R1.cer.
//
// TODO: this is a workaround; fix it properly and delete this file:
//   1. Get MIT to send the right chain for classrooms.mit.edu (IS&T, servicedesk@mit.edu). It's fixed once
//      `openssl s_client -connect classrooms.mit.edu:443 -showcerts` lists "SSL.com TLS Issuing RSA CA R1" as cert 1.
//   2. Find out why the prod Mac can't download the intermediate when this Mac can (blocked access to cert.ssl.com,
//      an older macOS, or how the bot is launched). Test on prod with:
//      CURL_SSL_BACKEND=secure-transport curl -sS -o /dev/null -w '%{http_code}\n' https://classrooms.mit.edu/
// Also, supplying any CAs makes curl use only those, so while this is in place these connections ignore the macOS
// keychain and trust /etc/ssl/cert.pem plus this intermediate instead.
inline constexpr const char* SSLCOM_TLS_ISSUING_RSA_CA_R1 = R"(-----BEGIN CERTIFICATE-----
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
iTZinEPk
-----END CERTIFICATE-----
)";

// The system's root CAs plus the intermediate above, or empty if the system bundle can't be read. Supplying CAs
// replaces the system trust store rather than adding to it, so the roots have to come along.
inline const std::string& ca_bundle() {
    static const std::string bundle = [] {
        std::ifstream roots("/etc/ssl/cert.pem");
        std::stringstream ss;
        ss << roots.rdbuf();
        return ss.str().empty() ? std::string() : ss.str() + "\n" + SSLCOM_TLS_ISSUING_RSA_CA_R1;
    }();
    return bundle;
}

// A libtouchstone session that also trusts the intermediates MIT's servers sometimes forget to send.
inline cpr::Session touchstone_session() {
    cpr::Session s = libtouchstone::session(config::cookiefile());
    const std::string& bundle = ca_bundle();
    if (!bundle.empty()) {
        curl_blob blob{(void*)bundle.data(), bundle.size(), CURL_BLOB_COPY};
        curl_easy_setopt(s.GetCurlHolder()->handle, CURLOPT_CAINFO_BLOB, &blob);
    }
    return s;
}

}
