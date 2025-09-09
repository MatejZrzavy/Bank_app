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
#include <memory>

#include <jdbc/mysql_driver.h>
#include <jdbc/mysql_connection.h>
#include <jdbc/cppconn/statement.h>
#include <jdbc/cppconn/prepared_statement.h>
#include <jdbc/cppconn/resultset.h>

using namespace std;
using namespace sql;

// Utility class - základní funkce
class Utils {
public:
    static void Mezera() {
        cout << "\n" << endl;
    }
    
    static string toLower(const string& text) {
        string result = text;
        transform(result.begin(), result.end(), result.begin(), ::tolower);
        return result;
    }
    
    static string formatText(const string& text) {
        if (!text.empty()) {
            string formatted = text;
            transform(formatted.begin(), formatted.end(), formatted.begin(), ::tolower);
            if (!formatted.empty()) {
                formatted[0] = toupper(formatted[0]);
            }
            return formatted;
        }
        return text;
    }
};

// Transaction class  - jednotlivé transakce
class Transaction {
private:
    int id;
    string description;
    double amount;
    string date;
    int categoryId;
    int userId;
    int sign;
    
public:
    Transaction(int id, const string& desc, double amt, const string& dt, 
                int catId, int usrId, int sgn)
        : id(id), description(desc), amount(amt), date(dt), 
          categoryId(catId), userId(usrId), sign(sgn) {}
    

          int getId() const { return id; }
    string getDescription() const { return description; }
    double getAmount() const { return amount; }
    string getDate() const { return date; }
    int getCategoryId() const { return categoryId; }
    int getUserId() const { return userId; }
    int getSign() const { return sign; }
    
    // zobrazení transakcí
    void display(const string& categoryName = "") const {
        cout << id << " | " << description << " | " << amount << " | " << date << " | " << categoryName << endl;
    }
};

// Category class
class Category {
private:
    int id;
    string name;
    int userId;
    
public:
    Category(int id, const string& name, int userId): id(id), name(name), userId(userId) {}

    int getId() const { return id; }
    string getName() const { return name; }
    int getUserId() const { return userId; }
};

// Database manager class - vytvoření tabulek & připojení k db
class DatabaseManager {
private:
    unique_ptr<Connection> connection;
    
public:
    DatabaseManager(const string& host, const string& username, const string& password, const string& database) {
        try {
            mysql::MySQL_Driver *driver = mysql::get_mysql_driver_instance();
            connection.reset(driver->connect(host, username, password));
            connection->setSchema(database);
        } catch (SQLException &e) {
            throw runtime_error("Database connection failed: " + string(e.what()));
        }
    }
    
    Connection* getConnection() {
        return connection.get();
    }
    
    void createTables() {
        unique_ptr<Statement> stmt(connection->createStatement());
        
        stmt->execute("CREATE TABLE IF NOT EXISTS users ("
                     "id INT AUTO_INCREMENT PRIMARY KEY,"
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
};

// User class
class User {
private:
    int id;
    string username;
    double monthlyBudget;
    DatabaseManager* dbManager;
    
public:
    User(const string& username, DatabaseManager* dbMgr) 
        : username(username), monthlyBudget(0), dbManager(dbMgr) {
        createOrLoadUser();
    }
    
    int getId() const { return id; }
    string getUsername() const { return username; }
    double getMonthlyBudget() const { return monthlyBudget; }
    
private:
    void createOrLoadUser() {
        Connection* con = dbManager->getConnection();
        
        unique_ptr<PreparedStatement> getID(con->prepareStatement(
            "SELECT id FROM users WHERE username = ?"));
        getID->setString(1, username);
        unique_ptr<ResultSet> res(getID->executeQuery());
        
        if (res->next()) {
            id = res->getInt("id");
            cout << "Uživatel " << username << " již existuje" << endl;
            loadBudget();
        } else {
            unique_ptr<PreparedStatement> pstmt(con->prepareStatement(
                "INSERT INTO users (username) VALUES (?)"));
            pstmt->setString(1, username);
            pstmt->execute();
            
            unique_ptr<PreparedStatement> getNewID(con->prepareStatement(
                "SELECT id FROM users WHERE username = ?"));
            getNewID->setString(1, username);
            unique_ptr<ResultSet> resNew(getNewID->executeQuery());
            
            if (resNew->next()) {
                id = resNew->getInt("id");
            } else {
                throw runtime_error("Chyba při získávání ID nového uživatele.");
            }
            cout << "Uživatel " << username << " byl vytvořen." << endl;
        }
        Utils::Mezera();
    }
    
    void loadBudget() {
        Connection* con = dbManager->getConnection();
        unique_ptr<PreparedStatement> pstmt(con->prepareStatement(
            "SELECT budget FROM monthly_budget WHERE user_id = ?"));
        pstmt->setInt(1, id);
        unique_ptr<ResultSet> res(pstmt->executeQuery());
        
        if (res->next()) {
            monthlyBudget = res->getDouble("budget");
            cout << "Měsíční rozpočet je nastaven na: " << monthlyBudget << endl;
        }
    }
    
public:
    void setBudget(double budget) {
        // kontrola nezápornosti je hotová
        Connection* con = dbManager->getConnection();
        unique_ptr<PreparedStatement> pstmt(con->prepareStatement(
            "INSERT INTO monthly_budget (user_id, budget) VALUES (?, ?) ON DUPLICATE KEY UPDATE budget = ?"));
        pstmt->setInt(1, id);
        pstmt->setDouble(2, budget);
        pstmt->setDouble(3, budget);
        pstmt->execute();
        con->commit();
        
        monthlyBudget = budget;
        cout << "Měsíční rozpočet je nastaven na " << budget << "." << endl;
    }
};

// Transaction Manager class
class TransactionManager {
private:
    DatabaseManager* dbManager;
    
public:
    TransactionManager(DatabaseManager* dbMgr) : dbManager(dbMgr) {}
    
    void addTransaction(int userId) {
        string description, amountStr, date;
        double amount;
        int sign = 1;
        
        cout << "Pro návrat do menu napište slovo 'ZPET'.\n" << endl;
        
        while (true) {
            cout << "Popis výdaje/příjmu:" << endl;
            getline(cin, description);
            
            if (Utils::toLower(description) == "zpet") return;
            if (description.empty()) {
                cout << "Popis nesmí být prázdný." << endl;
                continue;
            }
            
            while (true) {
                cout << "Částka (příjem zadej jako kladné číslo, výdaj jako záporné): \n";
                cin >> amountStr;
                
                if (Utils::toLower(amountStr) == "zpet") return;
                
                try {
                    amount = stod(amountStr);
                    break;
                } catch (const invalid_argument& e) {
                    cout << "Zadej platnou hodnotu." << endl;
                }
            }
            
            sign = (amount < 0) ? -1 : 1;
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            
            cout << "\nDatum transakce je aktuální datum? (a/n): " << endl;
            string choice;
            cin >> choice;
            
            if (Utils::toLower(choice) == "zpet") return;
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            
            if (Utils::toLower(choice) == "n") {
                cout << "\nZadej datum ve formátu YYYY-MM-DD:" << endl;
                getline(cin, date);
                if (Utils::toLower(date) == "zpet") return;
            }
            
            string category;
            cout << "\nZadej kategorii: " << endl;
            getline(cin, category);
            
            if (Utils::toLower(category) == "zpet") return;
            
            int categoryId = getOrCreateCategory(userId, category);
            insertTransaction(userId, description, amount, sign, date, categoryId, Utils::toLower(choice) == "n");
            
            cout << "\nVýdaj uložen!" << endl;
            checkBudgetWarning(userId);
            Utils::Mezera();
        }
    }
    
    double calculateSum(int userId, bool isExpense, int days = -1) {
        Connection* con = dbManager->getConnection();
        unique_ptr<PreparedStatement> pstmt;
        
        if (days == -1) {
            pstmt.reset(con->prepareStatement(
                "SELECT SUM(amount) AS total FROM expenses WHERE user_id = ? AND sgn = ?"));
        } else {
            pstmt.reset(con->prepareStatement(
                "SELECT SUM(amount) AS total FROM expenses WHERE user_id = ? AND sgn = ? AND expense_date >= DATE_SUB(NOW(), INTERVAL ? DAY)"));
        }
        
        pstmt->setInt(1, userId);
        pstmt->setInt(2, isExpense ? -1 : 1);
        
        if (days != -1) {
            pstmt->setInt(3, days);
        }
        
        unique_ptr<ResultSet> res(pstmt->executeQuery());
        
        if (res->next()) {
            return res->getDouble("total");
        }
        return 0;
    }
    
    void showTransactions(int userId) {
        Connection* con = dbManager->getConnection();
        unique_ptr<PreparedStatement> pstmt(con->prepareStatement(
            "SELECT e.id, e.description, e.amount, e.expense_date, c.name AS category_name "
            "FROM expenses e "
            "LEFT JOIN category c ON e.category_id = c.id "
            "WHERE e.user_id = ?"));
        pstmt->setInt(1, userId);
        unique_ptr<ResultSet> res(pstmt->executeQuery());
        
        if (!res->next()) {
            cout << "Žádné transakce nenalezeny.\n" << endl;
            return;
        }
        
        cout << "ID | Popis | Částka | Datum | Kategorie \n" << endl;
        res->beforeFirst();
        
        while (res->next()) {
            Transaction trans(
                res->getInt("id"),
                res->getString("description"),
                res->getDouble("amount"),
                res->getString("expense_date"),
                0,
                userId,
                0
            );
            trans.display(res->getString("category_name"));
        }
        Utils::Mezera();
    }
    
    void deleteTransaction(int userId) {
        string description, amountStr;
        double amount;
        
        cout << "Pro krok zpět napište slovo 'ZPET'.\n" << endl;
        cout << "Zadej popis transakce, kterou chceš smazat: " << endl;
        getline(cin, description);
        
        if (Utils::toLower(description) == "zpet") {
            Utils::Mezera();
            return;
        }
        
        cout << "Zadej částku této transakce: " << endl;
        cin >> amountStr;
        
        if (Utils::toLower(amountStr) == "zpet") {
            Utils::Mezera();
            return;
        }
        
        try {
            amount = stod(amountStr);
        } catch (const invalid_argument& e) {
            cout << "Zadej platnou hodnotu." << endl;
            return;
        }
        
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        
        vector<Transaction> matchingTransactions = findTransactions(userId, description, amount);
        
        if (matchingTransactions.empty()) {
            cout << "Žádná transakce nebyla nalezena." << endl;
            Utils::Mezera();
            return;
        }
        
        int deleteId;
        if (matchingTransactions.size() == 1) {
            deleteId = matchingTransactions[0].getId();
            cout << "Transakce byla úspěšně vymazána." << endl;
        } else {
            cout << "ID | Popis | Částka | Datum \n" << endl;
            for (const auto& trans : matchingTransactions) {
                trans.display();
            }
            
            cout << "Zadej ID transakce, kterou chceš vymazat: " << endl;
            cin >> deleteId;
            
            bool validId = false;
            for (const auto& trans : matchingTransactions) {
                if (trans.getId() == deleteId) {
                    validId = true;
                    break;
                }
            }
            
            if (!validId) {
                cout << "Neplatné ID transakce. Operace zrušena." << endl;
                Utils::Mezera();
                return;
            }
            
            cout << "Výdaj s id: " << deleteId << " byl úspěšně vymazán." << endl;
        }
        
        executeDelete(deleteId, userId);
        Utils::Mezera();
    }
    
private:
    int getOrCreateCategory(int userId, const string& categoryName) {
        Connection* con = dbManager->getConnection();
        
        unique_ptr<PreparedStatement> getCatId(con->prepareStatement(
            "SELECT id FROM category WHERE name = ? AND user_id = ?"));
        getCatId->setString(1, Utils::formatText(categoryName));
        getCatId->setInt(2, userId);
        unique_ptr<ResultSet> res(getCatId->executeQuery());
        
        if (res->next()) {
            return res->getInt("id");
        }
        
        // vytvoření nové kategorie
        unique_ptr<PreparedStatement> createCat(con->prepareStatement(
            "INSERT INTO category (name, user_id) VALUES (?, ?)"));
        createCat->setString(1, Utils::formatText(categoryName));
        createCat->setInt(2, userId);
        createCat->execute();
        
        unique_ptr<PreparedStatement> getNewId(con->prepareStatement(
            "SELECT id FROM category WHERE name = ? AND user_id = ?"));
        getNewId->setString(1, Utils::formatText(categoryName));
        getNewId->setInt(2, userId);
        unique_ptr<ResultSet> newRes(getNewId->executeQuery());
        
        if (newRes->next()) {
            return newRes->getInt("id");
        }
        
        return 0;
    }
    
    void insertTransaction(int userId, const string& description, double amount, 
                          int sign, const string& date, int categoryId, bool hasCustomDate) {
        Connection* con = dbManager->getConnection();
        unique_ptr<PreparedStatement> pstmt;
        
        if (hasCustomDate) {
            pstmt.reset(con->prepareStatement(
                "INSERT INTO expenses (description, amount, user_id, sgn, expense_date, category_id) VALUES (?, ?, ?, ?, ?, ?)"));
            pstmt->setString(5, date);
            pstmt->setInt(6, categoryId);
        } else {
            pstmt.reset(con->prepareStatement(
                "INSERT INTO expenses (description, amount, user_id, sgn, category_id) VALUES (?, ?, ?, ?, ?)"));
            pstmt->setInt(5, categoryId);
        }
        
        pstmt->setString(1, Utils::formatText(description));
        pstmt->setDouble(2, amount);
        pstmt->setInt(3, userId);
        pstmt->setInt(4, sign);
        
        pstmt->executeUpdate();
        con->commit();
    }
    
    void checkBudgetWarning(int userId) {
        double totalMoney = calculateSum(userId, true, 30) + calculateSum(userId, false, 30);
        
        Connection* con = dbManager->getConnection();
        unique_ptr<PreparedStatement> budgetPstmt(con->prepareStatement(
            "SELECT budget FROM monthly_budget WHERE user_id = ?"));
        budgetPstmt->setInt(1, userId);
        unique_ptr<ResultSet> budgetRes(budgetPstmt->executeQuery());
        
        double monthlyBudget = 0;
        if (budgetRes->next()) {
            monthlyBudget = budgetRes->getDouble("budget");
        }
        
        if (monthlyBudget != 0 && totalMoney < -monthlyBudget) {
            cout << "\nUpozornění: Výdaje překračují tvůj měsíční rozpočet o " 
                 << -(totalMoney + monthlyBudget) << " !" << endl;
        }
    }
    
    vector<Transaction> findTransactions(int userId, const string& description, double amount) {
        Connection* con = dbManager->getConnection();
        unique_ptr<PreparedStatement> pstmt(con->prepareStatement(
            "SELECT id, description, amount, expense_date FROM expenses WHERE user_id = ? AND description = ? AND amount = ?"));
        pstmt->setInt(1, userId);
        pstmt->setString(2, Utils::formatText(description));
        pstmt->setDouble(3, amount);
        unique_ptr<ResultSet> res(pstmt->executeQuery());
        
        vector<Transaction> transactions;
        while (res->next()) {
            transactions.emplace_back(
                res->getInt("id"),
                res->getString("description"),
                res->getDouble("amount"),
                res->getString("expense_date"),
                0, userId, 0
            );
        }
        
        return transactions;
    }
    
    void executeDelete(int transactionId, int userId) {
        Connection* con = dbManager->getConnection();
        int categoryId = 0;

        //zjistím kategorii transakce
        unique_ptr<PreparedStatement> getCatStmt(con->prepareStatement(
        "SELECT category_id FROM expenses WHERE id = ? AND user_id = ?"));
        getCatStmt->setInt(1, transactionId);
        getCatStmt->setInt(2, userId);
        unique_ptr<ResultSet> catRes(getCatStmt->executeQuery());
        if (catRes->next()) {
            categoryId = catRes->getInt("category_id");
        }

        // smazání transakce
        unique_ptr<PreparedStatement> delStmt(con->prepareStatement(
            "DELETE FROM expenses WHERE id = ? AND user_id = ?"));
        delStmt->setInt(1, transactionId);
        delStmt->setInt(2, userId);
        delStmt->execute();

        // Kontrola, jestli je ještě nějaká transakce v kategorii - ne -> smazat
        if (categoryId > 0) {
            unique_ptr<PreparedStatement> checkStmt(con->prepareStatement(
                "SELECT COUNT(*) as count FROM expenses WHERE category_id = ? AND user_id = ?"));
            checkStmt->setInt(1, categoryId);
            checkStmt->setInt(2, userId);
            unique_ptr<ResultSet> checkRes(checkStmt->executeQuery());
            
            if (checkRes->next() && checkRes->getInt("count") == 0) {
                // Žádné transakce v kategorii nezůstaly -> smazat
                unique_ptr<PreparedStatement> delCatStmt(con->prepareStatement(
                    "DELETE FROM category WHERE id = ? AND user_id = ?"));
                delCatStmt->setInt(1, categoryId);
                delCatStmt->setInt(2, userId);
                delCatStmt->execute();
            }
        }

        con->commit();
    }
};

// Category Manager class
class CategoryManager {
private:
    DatabaseManager* dbManager;
    
public:
    CategoryManager(DatabaseManager* dbMgr) : dbManager(dbMgr) {}
    
    void filterByCategory(int userId) {
        cout << "Dané kategorie jsou: " << endl;
        
        Connection* con = dbManager->getConnection();
        unique_ptr<PreparedStatement> pstmt(con->prepareStatement(
            "SELECT name FROM category WHERE user_id = ?"));
        pstmt->setInt(1, userId);
        unique_ptr<ResultSet> res(pstmt->executeQuery());
        
        if (!res->next()) {
            cout << " - Žádná kategorie nebyla nalezena." << endl;
            return;
        }
        
        res->beforeFirst();
        while (res->next()) {
            cout << " - " << res->getString("name") << endl;
        }
        Utils::Mezera();
        
        string category;
        cout << "Zadej kategorii, kterou chceš filtrovat: " << endl;
        getline(cin, category);
        
        showTransactionsByCategory(userId, category);
    }
    
private:
    void showTransactionsByCategory(int userId, const string& categoryName) {
        Connection* con = dbManager->getConnection();
        unique_ptr<PreparedStatement> pstmt(con->prepareStatement(
            "SELECT e.description, e.amount, e.expense_date, c.name AS category_name "
            "FROM expenses e "
            "LEFT JOIN category c ON e.category_id = c.id "
            "WHERE e.user_id = ? AND c.name = ?"));
        pstmt->setInt(1, userId);
        pstmt->setString(2, Utils::formatText(categoryName));
        
        unique_ptr<ResultSet> res(pstmt->executeQuery());
        
        if (!res->next()) {
            cout << "Žádné výdaje v této kategorii nejsou." << endl;
            return;
        }
        
        cout << "Výdaje v kategorii " << categoryName << ":\n" << endl;
        cout << "Popis | Částka | Datum \n" << endl;
        
        res->beforeFirst();
        while (res->next()) {
            cout << res->getString("description") << " | " << res->getDouble("amount") << " | " << res->getString("expense_date") << endl;
        }
        Utils::Mezera();
        
        cout << "Přeješ si sečíst transakce v této kategorii? (a/n): " << endl;
        string choice;
        getline(cin, choice);
        
        if (choice == "a" || choice == "A") {
            calculateCategorySum(userId, categoryName);
        }
    }
    
    void calculateCategorySum(int userId, const string& categoryName) {
        Connection* con = dbManager->getConnection();
        unique_ptr<PreparedStatement> sumStmt(con->prepareStatement(
            "SELECT SUM(amount) AS total_category FROM expenses e "
            "LEFT JOIN category c ON e.category_id = c.id "
            "WHERE e.user_id = ? AND c.name = ?"));
        sumStmt->setInt(1, userId);
        sumStmt->setString(2, categoryName);
        
        unique_ptr<ResultSet> sumRes(sumStmt->executeQuery());
        
        if (sumRes->next()) {
            double totalCategory = sumRes->getDouble("total_category");
            if (totalCategory < 0) {
                cout << "Celkové výdaje v kategorii " << categoryName 
                     << " jsou: " << abs(totalCategory) << endl;
            } else {
                cout << "Celkové příjmy v kategorii " << categoryName 
                     << " jsou: " << totalCategory << endl;
            }
        }
    }
};

class FileManager {
private:
    DatabaseManager* dbManager;
    
public:
    FileManager(DatabaseManager* dbMgr) : dbManager(dbMgr) {}
    
    void exportToCSV(int userId) {
        string fileName;
        cout << "Jak chcete pojmenovat soubor?" << endl;
        getline(cin, fileName);
        
        // .csv, abych měl všechny soubory stejný
        size_t dotPos = fileName.find_last_of(".");
        if (dotPos == string::npos) {
            fileName += ".csv";
        } else {
            string extension = fileName.substr(dotPos);
            if (extension != ".csv") {
                fileName = fileName.substr(0, dotPos) + ".csv";
            }
        }
        
        Connection* con = dbManager->getConnection();
        unique_ptr<PreparedStatement> pstmt(con->prepareStatement(
            "SELECT e.description, e.amount, e.expense_date, c.name AS category_name "
            "FROM expenses e "
            "LEFT JOIN category c ON e.category_id = c.id "
            "WHERE e.user_id = ?"));
        pstmt->setInt(1, userId);
        unique_ptr<ResultSet> res(pstmt->executeQuery());
        
        ofstream file(fileName);
        if (!file.is_open()) {
            cout << "Chyba při otevírání souboru pro zápis." << endl;
            return;
        }
        
        file << "Popis,Částka,Datum,Kategorie\n";
        while (res->next()) {
            file << res->getString("description") << ","
                 << fixed << setprecision(2) << res->getDouble("amount") << ","
                 << res->getString("expense_date") << ","
                 << res->getString("category_name") << "\n";
        }
        
        file.close();
        cout << "Data byla úspěšně exportována do souboru: " << fileName << endl;
    }
    
    void importFromCSV(int userId) {
        string fileName;
        cout << "Vkládejte soubory, kde jsou transakce tvaru: 'popis, částka, datum, kategorie'.\n" << endl;
        cout << "Napište název souboru, který chcete otevřít - včetně přípony: (nazev_souboru.csv)" << endl;
        getline(cin, fileName);
        
        ifstream file(fileName);
        if (!file.is_open()) {
            cout << "Chyba při otevírání souboru." << endl;
            return;
        }
        
        string line;
        while (getline(file, line)) {
            processCSVLine(line, userId);
        }
        
        file.close();
        dbManager->getConnection()->commit();
        cout << "Data byla úspěšně importována ze souboru: " << fileName << endl;
    }
    
private:
    void processCSVLine(const string& line, int userId) {
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
            return; // zkouším, jestli mám hlavičku
        }
        
        int categoryId = 0;
        if (!category.empty()) {
            categoryId = getOrCreateCategory(userId, category);
        }
        
        int sign = (amount < 0) ? -1 : 1;
        
        Connection* con = dbManager->getConnection();
        unique_ptr<PreparedStatement> pstmt(con->prepareStatement(
            "INSERT INTO expenses (description, amount, user_id, expense_date, sgn, category_id) VALUES (?, ?, ?, ?, ?, ?)"));
        pstmt->setString(1, Utils::formatText(description));
        pstmt->setDouble(2, amount);
        pstmt->setInt(3, userId);
        pstmt->setString(4, date);
        pstmt->setInt(5, sign);
        
        if (categoryId > 0) {
            pstmt->setInt(6, categoryId);
        } else {
            pstmt->setNull(6, sql::DataType::INTEGER);
        }
        
        pstmt->executeUpdate();
    }
    
    int getOrCreateCategory(int userId, const string& categoryName) {
        Connection* con = dbManager->getConnection();
        
        unique_ptr<PreparedStatement> getCatId(con->prepareStatement(
            "SELECT id FROM category WHERE name = ? AND user_id = ?"));
        getCatId->setString(1, Utils::formatText(categoryName));
        getCatId->setInt(2, userId);
        unique_ptr<ResultSet> res(getCatId->executeQuery());
        
        if (res->next()) {
            return res->getInt("id");
        }
        
        // vytvoření nové kategorie
        unique_ptr<PreparedStatement> createCat(con->prepareStatement(
            "INSERT INTO category (name, user_id) VALUES (?, ?)"));
        createCat->setString(1, Utils::formatText(categoryName));
        createCat->setInt(2, userId);
        createCat->execute();
        
        unique_ptr<PreparedStatement> getNewId(con->prepareStatement(
            "SELECT id FROM category WHERE name = ? AND user_id = ?"));
        getNewId->setString(1, Utils::formatText(categoryName));
        getNewId->setInt(2, userId);
        unique_ptr<ResultSet> newRes(getNewId->executeQuery());
        
        if (newRes->next()) {
            return newRes->getInt("id");
        }
        
        return 0;
    }
};

// Main class
class BankingApp {
private:
    unique_ptr<DatabaseManager> dbManager;
    unique_ptr<User> currentUser;
    unique_ptr<TransactionManager> transactionManager;
    unique_ptr<CategoryManager> categoryManager;
    unique_ptr<FileManager> fileManager;
    
public:
    BankingApp(const string& host, const string& username, const string& password, const string& database) {
        try {
            dbManager = make_unique<DatabaseManager>(host, username, password, database);
            dbManager->createTables();
            
            transactionManager = make_unique<TransactionManager>(dbManager.get());
            categoryManager = make_unique<CategoryManager>(dbManager.get());
            fileManager = make_unique<FileManager>(dbManager.get());
            
            cout << "Jsi připojen k databázi. Operaci vybereš stisknutím dané číslice.\n" << endl;
        } catch (const exception& e) {
            throw runtime_error("Failed to initialize banking app: " + string(e.what()));
        }
    }
    
    void run() {
        try {
            initializeUser();
            SwitchChoice();
        } catch (const exception& e) {
            cerr << "Chyba: " << e.what() << endl;
        }
    }
    
private:
    void initializeUser() {
        string username;
        do {
            cout << "Zadej uživatelské jméno:" << endl;
            getline(cin, username);
        } while (username.empty());
        
        Utils::Mezera();
        currentUser = make_unique<User>(username, dbManager.get());
    }
    

void SwitchChoice() {
    string choiceStr;
    int choice = 0;
    
    while (choice != 10) {
        string menu = "1. Přidej výdaj/příjem\n"
                     "2. Sečti výdaje\n"
                     "3. Sečti příjmy\n"
                     "4. Vypiš celkový zisk/ztrátu\n"
                     "5. Odeber transakci\n"
                     "6. Tabulka transakcí\n"
                     "7. Nastavení měsíčního rozpočtu\n"
                     "8. Filtrování podle kategorie\n"
                     "9. Import a export tabulky\n"
                     "10. Konec programu";
        
        cout << menu << endl;
        cout << "Zadej číslo operace (1-10): " << endl;
        cin >> choiceStr;
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        
        try { // mám fakt číslo
            choice = stoi(choiceStr);
        } catch (const invalid_argument& e) {
            choice = 0; // Neplatná volba
            cout << "Neplatná volba." << endl;
            Utils::Mezera();
            continue;
        }
        
        Utils::Mezera();
        try {
            switch (choice) {
                case 1:
                    transactionManager->addTransaction(currentUser->getId());
                    break;
                case 2:
                    handleSumExpenses();
                    break;
                case 3:
                    handleSumIncomes();
                    break;
                case 4:
                    handleTotalBalance();
                    break;
                case 5:
                    transactionManager->deleteTransaction(currentUser->getId());
                    break;
                case 6:
                    transactionManager->showTransactions(currentUser->getId());
                    break;
                case 7:
                    handleBudgetSetting();
                    break;
                case 8:
                    categoryManager->filterByCategory(currentUser->getId());
                    Utils::Mezera();
                    break;
                case 9:
                    handleImportExport();
                    break;
                case 10:
                    cout << "Konec programu." << endl;
                    Utils::Mezera();
                    break;
                default:
                    cout << "Neplatná volba." << endl;
                    Utils::Mezera();
                    break;
            }
        } catch (const exception& e) {
            cerr << "Chyba při zpracování volby: " << e.what() << endl;
            Utils::Mezera();
        }
    }
}
    

    void handleSumExpenses() {
        cout << "Za jaké období (dny) chceš sečíst výdaje? (Pro všechny výdaje zadej -1): " << endl;
        int days;
        cin >> days;
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        
        double expenses = transactionManager->calculateSum(currentUser->getId(), true, days);
        cout << "Výdaje za dané období jsou: " << abs(expenses) << endl;
        Utils::Mezera();
    }
    
    void handleSumIncomes() {
        cout << "Za jaké období (dny) chceš sečíst příjmy? (Pro všechny příjmy zadej -1): " << endl;
        int days;
        cin >> days;
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        
        double incomes = transactionManager->calculateSum(currentUser->getId(), false, days);
        cout << "Příjmy za dané období jsou: " << incomes << endl;
        Utils::Mezera();
    }
    
    void handleTotalBalance() {
        cout << "Za jaké období (dny) chceš sečíst transakce? (Pro všechny transakce zadej -1): " << endl;
        int days;
        cin >> days;
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        
        double expenses = transactionManager->calculateSum(currentUser->getId(), true, days);
        double incomes = transactionManager->calculateSum(currentUser->getId(), false, days);
        double total = incomes + expenses; // expense je <0
        
        string periodText = (days == -1) ? "" : " za " + to_string(days) + " dní";
        
        if (total < 0) {
            cout << "Celková ztráta" << periodText << " je: " << abs(total) << endl;
        } else {
            cout << "Celkový zisk" << periodText << " je: " << total << endl;
        }
        Utils::Mezera();
    }
    
    void handleBudgetSetting() {
        cout << "Zadej svůj měsíční rozpočet (neomezený rozpočet je 0) - při překročení budeš upozorněn: " << endl;
        string budgetStr;
        double budget;
        
        cin >> budgetStr;
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        //cout << budgetStr << endl; // kontrola
        
        try {
            budget = stod(budgetStr);
            if (budget < 0) {
                cout << "Zadej nezápornou hodnotu" << endl;
                Utils::Mezera();
                return;
            }

            currentUser->setBudget(budget);
          
        } catch (const invalid_argument& e) {
            cout << "Zadej platnou hodnotu." << endl;
            Utils::Mezera();
            return;
        } 
        Utils::Mezera();
    }
    
    void handleImportExport() {
        cout << "Kterou operaci chceš provést? (Import / Export)" << endl;
        string operation;
        getline(cin, operation);
        
        string op = Utils::toLower(operation);
        if (op == "import") {
            fileManager->importFromCSV(currentUser->getId());
        } else if (op == "export") {
            fileManager->exportToCSV(currentUser->getId());
        } else {
            cout << "Zadej platnou operaci" << endl;
        }
        Utils::Mezera();
    }
};

// Main function
int main() {
    try {
        // Doplnění vlastních hodnot
        BankingApp app("tcp://127.0.0.1:3306", "root", "SQLprojekt3", "bankapp");
        app.run();
    } catch (const exception& e) {
        cerr << "Fatální chyba: " << e.what() << endl;
        return 1;
    }
    
    return 0;
}