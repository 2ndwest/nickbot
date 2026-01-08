#pragma once

#include <dpp/dpp.h>
#include <sqlite3.h>

namespace commands {

void workrequest(const dpp::slashcommand_t& event, sqlite3* database);
void quickroom(const dpp::slashcommand_t& event);

}
