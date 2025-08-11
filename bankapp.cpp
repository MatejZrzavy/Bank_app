#include <iostream>
#include <string>
#include <stdexcept>
#include <cmath>
#include <limits>
#include <cctype>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <typeinfo>


#include <jdbc/mysql_driver.h>
#include <jdbc/mysql_connection.h>
#include <jdbc/cppconn/statement.h>
#include <jdbc/cppconn/prepared_statement.h>
#include <jdbc/cppconn/resultset.h>


using namespace std;
using namespace sql;


void mezera(){
    cout << "\n" << endl;
}

string ToLower(const string& popis) {
    string result = popis;
    transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}


string ChangeToSameFormat(const string& textak){
    if (!textak.empty()) { // převod na stejný fromát (Takhle)
            string textakFormatted = textak;
            transform(textakFormatted.begin(), textakFormatted.end(), 
            textakFormatted.begin(), ::tolower);
            if (!textakFormatted.empty()) {
            textakFormatted[0] = toupper(textakFormatted[0]);
        }
        return textakFormatted;
    } 
    return textak;
}


void CreateTables(Connection *con) {
    // Vytvoření výdajů & uživatelů, pokud neexistuje
            unique_ptr<Statement> stmt(con->createStatement());
            
            stmt->execute("CREATE TABLE IF NOT EXISTS users ("
                        "id INT AUTO_INCREMENT PRIMARY KEY," // automaticky generované ID -> je vždy různé
                        "username VARCHAR(255) UNIQUE NOT NULL)");

            stmt->execute("CREATE TABLE IF NOT EXISTS monthly_budget ("
                        "user_id INT PRIMARY KEY,"
                        "budget DECIMAL(15,2) DEFAULT NULL,"
                        "FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE)");

            stmt->execute("CREATE TABLE IF NOT EXISTS category ("
                        "id INT AUTO_INCREMENT PRIMARY KEY,"
                        "name VARCHAR(255) NOT NULL COLLATE utf8mb4_general_ci,"
                        "user_id INT,"
                        "UNIQUE KEY unique_category_per_user (name, user_id),"
                        "FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE)");

            stmt->execute("CREATE TABLE IF NOT EXISTS expenses ("
                        "id INT AUTO_INCREMENT PRIMARY KEY," 
                        "description VARCHAR(255),"
                        "amount DECIMAL(15,2),"
                        "expense_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
                        "user_id INT,"
                        "category_id INT,"
                        "sgn INT NOT NULL DEFAULT 1,"
                        "FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,"
                        "FOREIGN KEY (category_id) REFERENCES category(id) ON DELETE SET NULL)");
            

}


double WhatsTheBudget (Connection *con, int userID) {
    unique_ptr<PreparedStatement> pstmt(con->prepareStatement(
        "SELECT budget FROM monthly_budget WHERE user_id = ?"));
    pstmt->setInt(1, userID);
    unique_ptr<ResultSet> res(pstmt->executeQuery());
    if (res->next()) {
        double budget = res->getDouble("budget");
    return budget;
    }else{
        return 0; //budget není nastaven
    }
}


int CreateUser(Connection *con, const string &username) {
    unique_ptr <PreparedStatement> getID (con -> prepareStatement("SELECT id FROM users WHERE username = ?"));
            getID -> setString(1, username);
            unique_ptr<ResultSet> res(getID->executeQuery());
            int userID;
            if (res -> next()) { // uživatel existuje
                userID = res->getInt("id");
                cout << "Uživatel " << username << " již existuje" << endl;
                double budget = WhatsTheBudget(con, userID);
                cout << "Měsíční rozpočet je nastaven na: "<<  budget << endl;
            } else {    //uživatel neexistuje -> vytvoříme ho
                unique_ptr<PreparedStatement> pstmt(con->prepareStatement("INSERT INTO users (username) VALUES (?)"));
                pstmt->setString(1, username);
                pstmt->execute();

                unique_ptr<PreparedStatement> getID (con -> prepareStatement ("SELECT id FROM users WHERE username = ?"));
                getID -> setString(1, username);
                unique_ptr<ResultSet> resNew(getID->executeQuery());
                if (resNew->next()){
                userID = resNew -> getInt("id");// získej ID nového uživatele
                } else {
                    throw runtime_error("Chyba při získávání ID nového uživatele.");
                }
                cout << "Uživatel " << username << " byl vytvořen." << endl;//"id: " << userID << endl;
            }mezera();
    return userID;
}


int SumOfMoneyLastNDays(Connection *con, int userID, int stat, int days = -1) {
    unique_ptr<PreparedStatement> pstmt;
    
    if (days == -1){
        pstmt.reset(con->prepareStatement(
                "SELECT SUM(amount) AS total_expenses FROM expenses WHERE user_id = ? AND sgn = ?"));
    }else{
        pstmt.reset(con->prepareStatement(
        "SELECT SUM(amount) AS total_expenses FROM expenses WHERE user_id = ? AND sgn = ? AND expense_date >=  DATE_SUB(NOW(), INTERVAL ? DAY)"));
    }
    pstmt->setInt(1, userID);
    if (stat == 2){
        pstmt -> setInt(2, -1); // výdaje
    }else{
        pstmt -> setInt(2, 1); // příjmy
    }if (days != -1){
        pstmt->setInt(3, days);
    } 
    unique_ptr<ResultSet> res(pstmt->executeQuery());
    if (res->next()) {
        double totalExpenses = res->getDouble("total_expenses");
        return totalExpenses;
    } else {
        cout << "Žádné výdaje nenalezeny." << endl;
        return 0;
    }
}


void ShowExpenses(Connection *con, int userID) {
    unique_ptr<PreparedStatement> pstmt(con->prepareStatement(
    "SELECT e.description, e.amount, e.expense_date, c.name AS category_name "
    "FROM expenses e "
    "LEFT JOIN category c ON e.category_id = c.id "
    "WHERE e.user_id = ?"));
    pstmt->setInt(1, userID);
    unique_ptr<ResultSet> res(pstmt->executeQuery());

    //cout << "Výdaje a příjmy:" << endl;
    if (!res->next()) {
        cout << "Žádné transakce nenalezeny.\n" << endl;
        return;
    }
    cout << "Popis | Částka | Datum | Kategorie \n" << endl;
    res->beforeFirst(); // resetování kurzoru na začátek
    while (res->next()) {
        string description = res->getString("description");
        double amount = res->getDouble("amount");
        string date = res->getString("expense_date");
        string category = res->getString("category_name");
        cout << description << " | "<<  amount << " | " << date  << " | " << category << endl;
    }mezera();
}


void DeleteExpense(Connection *con, int userID) {
    string description, amountSTR;
    double amount;
    int deleteID;
    int found = 0;
    int vytisknuto = 1;
    vector<int> validIDs;
     bool validID = false;

    cout << "Pro krok zpět napište slovo 'ZPET'.\n" << endl;
    cout << "Zadej popis transakce, kterou chceš smazat: " << endl;
    getline(cin, description);
    if (ToLower(description) == "zpet"){
        mezera();
        return;
    }
    cout << "Zadej částku této transakce, kterou chceš smazat (jde-li o výdaj, zadej s mínusem): " << endl;
    cin >> amountSTR;
    if (ToLower(amountSTR) == "zpet"){
        mezera();
        return;
    }
    try {
        amount = stod(amountSTR);
    } catch (const invalid_argument& e) {
        cout << "Zadej platnou hodnotu." << endl;
        return;
    }
    
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
    unique_ptr<PreparedStatement> pstmt(con->prepareStatement(
    "SELECT id, description, amount, expense_date FROM expenses WHERE user_id = ? AND description = ? AND amount = ?"));
    pstmt -> setInt(1, userID);
    pstmt -> setString(2, ChangeToSameFormat(description));
    pstmt -> setDouble(3, amount);
    unique_ptr < ResultSet> res(pstmt->executeQuery());
    
    mezera();
    while(res->next()){
        if(vytisknuto){
            cout << "ID | Popis | Částka | Datum \n" << endl;
        }
        vytisknuto = 0;
        found++;
        int id = res->getInt("id");
        validIDs.push_back(id);
        string desc = res->getString("description");
        double amt = res->getDouble("amount");
        string date = res->getString("expense_date");
        
        cout << id << " | "<<  desc << " | " << amt  << " | " << date << "\n" << endl;
    }

    if (found == 0){
        cout << "Žádná transakce nebyla nalezena." << endl;
        mezera();
        return;
    }
    if (found == 1) {
        cout << "Transakce byla úspěšně vymazána." << endl;
        deleteID = validIDs[0];
    }else{
        cout << "Zadej ID transakce, kterou chceš vymazat: " << endl;
        cin >> deleteID;
    
        // Zkouším, jessli je deleteID validní
    for (int id : validIDs) {
        if (id == deleteID) {
            validID = true;
            break;
        }
    }
    if (!validID) {
        cout << "Neplatné ID transakce. Operace zrušena." << endl;
        mezera();
        return;
    }

    cout << "Výdaj s id: " << deleteID << " byl úspěšně vymazán." << endl;
    }
    unique_ptr<PreparedStatement> delStmt (con-> prepareStatement(
        "DELETE FROM expenses WHERE id = ? AND user_id = ?"
    ));

    delStmt -> setInt(1, deleteID);
    delStmt -> setInt(2, userID);
    delStmt -> execute();
    con -> commit();
    

    mezera();
}


void BudgetCheck(Connection *con, int userID){
    double budget;
    cout << "Zadej svůj měsíční rozpočet (neomezený ropočet je 0) - při překročení budeš upozorněn: " << endl;
    cin >> budget;
    cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    if (budget < 0) {
        cout << "Rozpočet nemůže být záporný." << endl;
        return; // opakuj, pokud je rozpočet záporný
    }
    unique_ptr<PreparedStatement> pstmt(con->prepareStatement(
        "INSERT INTO monthly_budget (user_id, budget) VALUES (?, ?) ON DUPLICATE KEY UPDATE budget = ?"));
    pstmt->setInt(1, userID);
    pstmt->setDouble(2, budget);
    pstmt->setDouble(3, budget);
    pstmt->execute();
    con->commit();
    
    cout << "Měsíční rozpočet je nastaven na " << budget << "." << endl;
}


void CategoryFilter(Connection *con, int userID){
    // Zobrazení kategorií
    cout << "Dané kategorie jsou: " << endl;
    unique_ptr<PreparedStatement> pstmt(con->prepareStatement(
        "SELECT name FROM category WHERE user_id = ?"));
    pstmt->setInt (1, userID);
    unique_ptr<ResultSet> res(pstmt->executeQuery());
    if (!res->next()){
        cout << " - Žádná kategorie nebyla nalezena." << endl;
        return;
    }else{
        //cout << "Kategorie: \n" << endl;
        res->beforeFirst();
        while (res->next()) {
            string category = res->getString("name");
            cout << " - " << category << endl;
        }mezera();
    }
    
    // Vypsání daných kategorií
    string category;
    cout << "Zadej kategorii, kterou chceš filtrovat: " << endl;
    getline(cin, category);

    unique_ptr<PreparedStatement> pst(con->prepareStatement(
        "SELECT e.description, e.amount, e.expense_date, c.name AS category_name "
        "FROM expenses e "
        "LEFT JOIN category c ON e.category_id = c.id "
        "WHERE e.user_id = ? AND c.name = ?"
        ));
        pst -> setInt (1, userID);
        pst -> setString(2, ChangeToSameFormat(category));
    
    unique_ptr<ResultSet> respst(pst->executeQuery());
    if (!respst->next()) {
        cout << "Žádné výdaje v této kategorii nejsou." << endl;
        return;
    } else {
        cout << "Výdaje v kategorii " << category << ":\n" << endl;
        cout << "Popis | Částka | Datum \n" << endl;
        respst->beforeFirst(); // kurzor na začátek
        while (respst->next()) {
            string description = respst->getString("description");
            double amount = respst->getDouble("amount");
            string date = respst->getString("expense_date");
            cout << description << " | "<<  amount << " | " << date << endl;
            }
    mezera();

    cout << "Přeješ si sečíst transakce v této kategorii? (a/n): " << endl;
    string choice;
    getline(cin, choice);
    if (choice == "a" || choice == "A") {
        unique_ptr<PreparedStatement> sumstat (con->prepareStatement(
            "SELECT SUM(amount) AS total_category FROM expenses e "
            "LEFT JOIN category c ON e.category_id = c.id "
            "WHERE e.user_id = ? AND c.name = ?"));
        sumstat->setInt(1, userID);
        sumstat->setString(2, category);
        unique_ptr<ResultSet> sumres(sumstat->executeQuery());
        if (sumres->next()) {
            double totalCategory = sumres->getDouble("total_category");
            if (totalCategory < 0) {
                cout << "Celkové výdaje v kategorii " << category << " jsou: " << abs(totalCategory) << endl;
            } else {
                cout << "Celkové příjmy v kategorii " << category << " jsou: " << totalCategory << endl;
            }
            //cout << "Celková příjmy/výdaje v kategorii " << category << " jsou: " << totalCategory << endl;
            }
        }  
    }
}


void ExportToCSV(Connection *con, int userID) {
    string fileName;
    cout << "Jak chcete pojmenovat soubor?" << endl;
    getline(cin, fileName);
    
    // Najdu poslední tečku a podle ní určím, zda má slubor příponu
    size_t dotPos = fileName.find_last_of(".");
    
    if (dotPos == string::npos) {
        // Žádná tečka -> přidej .csv
        fileName += ".csv";
    } else {
        // Kontrola, jestli je to csv -> jestli ne -> zmena na csv
        string extension = fileName.substr(dotPos);
        if (extension != ".csv") {
             fileName = fileName.substr(0, dotPos) + ".csv";
        }
    }

    unique_ptr<PreparedStatement> pstmt(con->prepareStatement(
        "SELECT e.description, e.amount, e.expense_date, c.name AS category_name "
        "FROM expenses e "
        "LEFT JOIN category c ON e.category_id = c.id "
        "WHERE e.user_id = ?"));
    pstmt->setInt(1, userID);
    unique_ptr<ResultSet> res(pstmt->executeQuery());

    ofstream file(fileName);
    if (!file.is_open()) {
        cout << "Chyba při otevírání souboru pro zápis." << endl;
        return;
    }

    file << "Popis,Částka,Datum,Kategorie\n";
    while (res->next()) { // Dokud mám data
        string description = res->getString("description");
        double amount = res->getDouble("amount");
        string date = res->getString("expense_date");
        string category = res->getString("category_name");

        file << description << "," << fixed << setprecision(2) << amount << "," 
             << date << "," << category << "\n";
    }
    
    file.close();
    cout << "Data byla úspěšně exportována do souboru: " << fileName << endl;
    
}


void ImportFromCSV(Connection *con, int userID) {
    string fileName;
    string line;

    cout << "Vkládejte soubory, kde jsou transakce tvaru: 'popis, částka, datum, kategorie'.\n" << endl;

    cout << "Napište název souboru, který chcete otevřít - včetně přípony: (nazev_souboru.csv)" << endl;
    getline(cin, fileName);

    ifstream file(fileName);
    if (!file.is_open()) {
        cout << "Chyba při otevírání souboru." << endl;
        return;
    }
         
    while (getline(file, line)) {
        stringstream ss(line);
        string description, amountStr, date, category;
        double amount;

        getline(ss, description, ',');
        getline(ss, amountStr, ',');
        getline(ss, date, ',');
        getline(ss, category);

        try {
            amount = stod(amountStr);
        } catch (const invalid_argument& e) {
            continue;
        }
        
        
       int categoryID = 0;
        if (!category.empty()) {
            unique_ptr<PreparedStatement> getCatID(con->prepareStatement(
                "SELECT id FROM category WHERE name = ? AND user_id = ?"));
            getCatID->setString(1, ChangeToSameFormat(category));
            getCatID->setInt(2, userID);
            unique_ptr<ResultSet> resCat(getCatID->executeQuery());
            
            if (resCat->next()) {
                categoryID = resCat->getInt("id");
            } else {
                // Vytvoř novou kategorii
                unique_ptr<PreparedStatement> createCat(con->prepareStatement(
                    "INSERT INTO category (name, user_id) VALUES (?, ?)"));
                createCat->setString(1, ChangeToSameFormat(category));
                createCat->setInt(2, userID);
                createCat->execute();
                
                // Získej ID nově vytvořené kategorie
                unique_ptr<PreparedStatement> getNewCatID(con->prepareStatement(
                    "SELECT id FROM category WHERE name = ? AND user_id = ?"));
                getNewCatID->setString(1, ChangeToSameFormat(category));
                getNewCatID->setInt(2, userID);
                unique_ptr<ResultSet> resNewCat(getNewCatID->executeQuery());
                if (resNewCat->next()) {
                    categoryID = resNewCat->getInt("id");
                }
            }
        }

        int sgn;
        if (amount < 0){
            sgn = -1;
        }else{
            sgn = 1;
        }

        // Vlož transakci do databáze
        unique_ptr<PreparedStatement> pstmt(con->prepareStatement(
            "INSERT INTO expenses (description, amount, user_id, expense_date, sgn, category_id) VALUES (?, ?, ?, ?, ?, ?)"));
        pstmt->setString(1, ChangeToSameFormat(description));
        pstmt->setDouble(2, amount);
        pstmt->setInt(3, userID);
        pstmt->setString(4, date);
        pstmt->setInt(5, sgn);
        if (categoryID > 0) {
            pstmt->setInt(6, categoryID);
        } else {
            pstmt->setNull(6, sql::DataType::INTEGER);
        }
        
        pstmt->executeUpdate();
    }
    
    file.close();
    con->commit();
    cout << "Data byla úspěšně importována ze souboru: " << fileName << endl;
}



void AddToExpenses(Connection *con, int userID) {
    string description;
    string amountSTR;
    double amount;
    string choice;
    int sgn = 1;
    int categoryID;
    string date;
    
    while (true) { 
        // Zadání popisu
        cout << "Popis výdaje/příjmu:" << endl;
        getline(cin, description);
        
        if (ToLower(description) == "zpet") {
            return; // zpět do hlavního menu
        }
        
        if (description.empty()) {
            cout << "Popis nesmí být prázdný." << endl;
            continue; // nedal popis -> znovu
        }
        
        // Zadání částky
        while (true) {
            cout << "Částka (příjem zadej jako kladné číslo, výdaj jako záporné (př.: -100)): \n";
            cin >> amountSTR;
            
            if (ToLower(amountSTR) == "zpet") {
                return;
            }
            
            // string na double
            try {
                amount = stod(amountSTR);
                break;
            } catch (const invalid_argument& e) {
                cout << "Zadej platnou hodnotu." << endl;
            }
        }
        
        if (amount < 0) {
            sgn = -1; // záporné číslo -> výdaj
        } else {
            sgn = 1; // kladné číslo -> příjem
        }
        cin.ignore(numeric_limits<streamsize>::max(), '\n');

        // Vložení dat do tabulky & datum transakce
        cout << "\nDatum transakce je aktuální datum? (a/n): " << endl;
        string choice;
        cin >> choice;
        
        if (ToLower(choice) == "zpet") {
            return;
        }
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        unique_ptr<PreparedStatement> pstmt;
        if (ToLower(choice) == "n") {
            cout << "\nZadej datum ve formátu YYYY-MM-DD:" << endl;
            getline(cin, date);
            if (ToLower(date) == "zpet") {
                return;
            }
            
            pstmt.reset(con->prepareStatement(
            "INSERT INTO expenses (description, amount, user_id, sgn, expense_date, category_id) VALUES (?, ?, ?, ?, ?, ?)"));
        } else if (ToLower(choice) == "a") {
            pstmt.reset(con->prepareStatement(
            "INSERT INTO expenses (description, amount, user_id, sgn, category_id) VALUES (?, ?, ?, ?, ?)"));
        } else {
            cout << "\nZadej platnou hodnotu." << endl;
            continue;
        }
        
        pstmt->setString(1, ChangeToSameFormat(description));
        pstmt->setDouble(2, amount); 
        pstmt->setInt(3, userID);
        pstmt->setInt(4, sgn);
        
        // pokud uživatel zvolil vlastní datum -> přidej ho
        if (ToLower(choice) == "n") {
            pstmt->setString(5, date);
        }

        // třídění do kategorií
        string category;
        int categoryID = 0;
        cout << "\nZadej kategorii: " << endl;
        getline(cin, category);
        
        if (ToLower(category) == "zpet") {
            return;
        }

        unique_ptr<PreparedStatement> getCatID(con->prepareStatement(
        "SELECT id FROM category WHERE name = ? AND user_id = ?"));
        getCatID->setString(1, ChangeToSameFormat(category));
        getCatID->setInt(2, userID);
        unique_ptr<ResultSet> resCat(getCatID->executeQuery());
        
        // Kontrola, zda kategorie již existuje 
        if (resCat->next()) {
            // Kategorie existuje
            categoryID = resCat->getInt("id");
        } else {
            // Kategorie neexistuje -> vytvořím
            unique_ptr<PreparedStatement> createCat(con->prepareStatement(
            "INSERT INTO category (name, user_id) VALUES (?, ?)"));
            createCat->setString(1, ChangeToSameFormat(category));
            createCat->setInt(2, userID);
            createCat->execute();
            
            // Získám ID nově vytvořené kategorie
            unique_ptr<PreparedStatement> getNewCatID(con->prepareStatement(
            "SELECT id FROM category WHERE name = ? AND user_id = ?"));
            getNewCatID->setString(1, ChangeToSameFormat(category));
            getNewCatID->setInt(2, userID);
            unique_ptr<ResultSet> resNewCat(getNewCatID->executeQuery());
            if (resNewCat->next()) {
                categoryID = resNewCat->getInt("id");
            }
        }
        
        if (ToLower(choice) == "n") { // mám vlastní datum -> 2 možnosti
            pstmt->setInt(6, categoryID);
        } else if (ToLower(choice) == "a") {
            pstmt->setInt(5, categoryID);
        }
        
        pstmt->executeUpdate();
        con->commit();
        cout << "\nVýdaj uložen!" << endl;

        // Kontrola měsíčního rozpočtu
        int totalMoney = SumOfMoneyLastNDays(con, userID, 2, 30) + SumOfMoneyLastNDays(con, userID, 3, 30);

        unique_ptr<PreparedStatement> budgetPstmt(con->prepareStatement(
            "SELECT budget FROM MONTHLY_BUDGET WHERE user_id = ?"));
        budgetPstmt->setInt(1, userID);
        unique_ptr<ResultSet> budgetRes(budgetPstmt->executeQuery());
        
        double monthly_budget = 0;
        if (budgetRes->next()) {
            monthly_budget = budgetRes->getDouble("budget");
        }
        if (monthly_budget != 0 && totalMoney < -(monthly_budget)) {
            cout << "\nUpozornění: Výdaje překračují tvůj měsíční rozpočet o " << -(totalMoney + monthly_budget) << " !" << endl;
        }
        mezera();
    }
}



int SwitchChoice(int stat, int userID, Connection *con) {
    string zprava;
    int days ;
    int money;
    string operation;
    zprava = "1. Přidej výdaj/příjem\n2. Sečti výdaje\n3. Sečti přijmy\n4. Vypiš celkový zisk/ztrátu\n5. Odeber transakci\n6. Tabulka transakcí\n7. Nastavení měsíčního rozpočtu\n8. Filtrování podle kategorie\n9. Import a export tabulky\n10. Konec programu";
    cout << zprava << endl;
    cout << "Zadej číslo operace (1-10): " << endl;
    cin >> stat;
    cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    mezera();
    switch (stat){
            
            // Přidej výdaj/příjem
            case 1: 
                cout << "Pro návrat do menu napiště slovo 'ZPET'.\n" << endl;
                AddToExpenses(con, userID);
                mezera();
                break;
            
            // Sečti výdaje
            case 2: 
                cout << "Za jaké období (dny) chceš sečíst výdaje? (Pro všechny výdaje zadej -1):  " << endl;
                cin >> days;
                cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                cout << "Výdaje za dané období jsou: " << SumOfMoneyLastNDays(con, userID, stat, days) << endl;
                mezera();
                break;
            
            // Sečti příjmy
            case 3: 
                cout << "Za jaké období (dny) chceš sečíst příjmy? (Pro všechny příjmy zadej -1):  " << endl;
                cin >> days;
                cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                cout << "Příjmy za dané období jsou: " << SumOfMoneyLastNDays(con, userID, stat, days) << endl;
                mezera();
                break;
            
            // Vypiš celkový zisk/ztrátu
            case 4: 
                cout << "Za jaké období (dny) chceš sečíst transakce? (Pro všechny transakce zadej -1):  " << endl;
                cin >> days;
                cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                money = SumOfMoneyLastNDays(con, userID, 3, days) + SumOfMoneyLastNDays(con, userID, 2, days);
                if (days == -1){
                    if (money < 0){
                        cout << "Celková ztráta je: " << abs(money) << endl;
                    }else{
                        cout << "Celkový zisk je: " << money << endl;
                    }
                }else{
                    if (money < 0){
                        cout << "Celková ztráta za " << days << " dní je: " << abs(money) << endl;
                    }else{
                        cout << "Celkový zisk za " << days << " dní je: " << money << endl;
                    }
                }
                mezera();
                break;

            // Odeber příjem/výdaj
            case 5:
                DeleteExpense(con, userID);
                //cout << "Odeber příjem/výdaj." << endl;
                break;
             
            // Ukaž tabulku
            case 6: 
                //cout << "Tabulka výdajů: " << endl;
                ShowExpenses(con, userID);
                break;
    
            // Nastavení měsíčního rozpočtu
            case 7:
                BudgetCheck(con, userID);
                mezera();
                break;
            
            //Filtrování podle kategorie
            case 8: 
                CategoryFilter(con, userID);
                mezera();
                break;
            
            // Import/Export
            case 9: 
                cout << "Kterou operaci chceš porvést? (Import / Export)" << endl;
                getline(cin, operation);
                operation = ToLower(operation);
                if (operation == "import"){
                    ImportFromCSV(con, userID);
                }else if (operation == "export"){
                    ExportToCSV(con, userID);
                }else{
                    cout << "Zadej platnou operaci" << endl;
                }
                mezera();
                break;

            // Konec programu
            case 10:
                cout << "Konec programu." << endl;
                mezera();
                break;
            
            default:
                cout << "Neplatná volba." << endl;
                mezera();
                return 1; // chyba
        }return stat;
    }


int main() {
    try {
        // Připojení k databázi & úvod
        mysql::MySQL_Driver *driver = mysql::get_mysql_driver_instance();
        
        // Doplnění vlastních údajů!!!
        unique_ptr<Connection> con(driver->connect("tcp://127.0.0.1:3306", "root", "SQLprojekt3")); // doplnění vlasntích hodnot
        
        
        con->setSchema("bankapp");
        cout << "Jsi připojen k databázi. Operaci vybereš stisknutím dané číslice.\n" << endl;
        //cout << "1. Přidej výdaj/příjem\n2. Sečti výdaje\n3. Sečti přijmy\n4. Vypiš celkový zisk/ztrátu\n5. Odeber příjem/výdaj\n6. Konec" << endl;
        

        // vytvoření tabulek, pokud neexistují
        CreateTables(con.get());

        
        // Chci, dokud neznám uživatele
        string username; 
        do{
        cout << "Zadej uživatelské jméno:" << endl;
        getline(cin, username);
        }
         while (username.empty());

        mezera();

        // Nastavení uživatele
        int userID;
        userID = CreateUser(con.get(), username);
        //cout << userID << endl; // kontrola ID


        int stat = 1;
        //cout << "Zadej číslo operace (1-6): " << endl;
        //cin >> stat;
        //SwitchChoice(stat, userID, con.get());


        //Hlavní program
        while (stat != 10) {
            string description;
            double amount;

            stat = SwitchChoice(stat, userID, con.get());
            
            /*cout << "1. Přidej výdaj/příjem\n2. Sečti výdaje\n3. Sečti přijmy\n4. Vypiš celkový zisk/ztrátu\n5. Odeber příjem/výdaj\n6. Konec" << endl;
            cout << "Zadej číslo operace (1-6): " << endl;
            cin >> stat;
            cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');*/
        }

    } catch (SQLException &e) { // chyba v databázi
        cerr << "Chyba SQL: " << e.what() << endl;
        return 1;
    } catch (exception &e) { // univerzální chyba
        cerr << "Chyba: " << e.what() << endl;
        return 1;
    }
    return 0;
    }
