#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <unordered_map>
#include <limits>

// тип фигур
enum class FigureType {
    PAWN = 1, 
    KNIGHT = 2, 
    BISHOP = 3, 
    ROOK = 4, 
    QUEEN = 5, 
    KING = 6
};

// вес фигур (заглушка, пешка, конь, слон, ладья, королева, король)
// 0 нужен как заглушка, чтобы было удобно обращаться к переменной с типом фигур
const int FIGURE_WEIGHTS[7] = {0, 1, 3, 3, 5, 9, 20000};

// клетка: x - буквы, y - цифарки
// валидность на то, что мы в пределах доски ([CHECK] пока оставить?)
// валидность того, что корректный строковый формат ([CHECK] пока оставить?)
// проверка на то, что это ТОЧНО другая клетка ([CHECK] пока оставить?)
struct Cell {
    char x = '\0', y = '\0';
    bool valid() const { return x >= 'a' && x <= 'h' && y >= '1' && y <= '8'; }
    std::string str() const { return valid() ? std::string() + x + y : ""; }
    bool operator==(const Cell& o) const { return x == o.x && y == o.y; }
};

// фигуры: позиция, цвет, тип фигуры
struct Figure {
    Cell pos;
    char color;  // '+' - наш или '-' - не наш хд
    FigureType type;
};

// то, шо получаем от сервера: мой прошлый ping, кол-во фигур, список всех фигур, взятие на проходе, рокировка 
struct InputData {
    int pong = 0;
    int count = 0;
    std::vector<Figure> figures;                        // (клетка, цвет, тип)
    Cell ep_square;
    bool castling[4] = {false, false, false, false};    // (длин мая, кор мая, длин немая, кор немая)
};

// наш ход: откуда и куда 
// преобразование в строку
// проверка шо значения по шахматной доске
struct Move {
    Cell from, to;
    std::string str() const { return from.str() + to.str(); }
    bool valid() const { return from.valid() && to.valid(); }
};



// библиотека дебютов, епта
class OpeningBook {
private:
    // хэээш :3333
    // история ходов - ключ; значения - мой готовый ход из библиотеки
    std::unordered_map<std::string, std::string> book;

public:
    OpeningBook() { load(); }

    // определение белые мы или черные происходит в начале игры, относительно того где стоит наш '+':
    // т.е., если в начале - мы белые; если в конце - мы черные
    // держим две библиотеки для дебютов за белых/черных
    void load() {
        // Базовые примеры для белых.
        //book[""] = "e2e4";
        //book["e2e4"] = "e7e5";
        //book["e2e4_e7e5"] = "g1f3";
        //book["e2e4_e7e5_g1f3_b8c6"] = "f1c4";

        // Пример для черных.
        // book["e2e4"] = "c7c5";
    }
    // имщем ключики
    std::string lookup(const std::string& position_key) const {
        auto it = book.find(position_key);                  // хэшируем, ищем совпадение ключей
        return (it != book.end()) ? it->second : "";        // вовзаращаем ход, если ключ найден, иначе пустую строку ([CHECK] потом исправить не на путсую?)
    }
};



// чтение ответа сервера
void read_input(InputData& data) {
    std::string line;

    // читаем pong
    if (!(std::cin >> data.pong)) return;

    // получаем кол-во фигур
    int n;
    std::cin >> n;
    data.count = n;

    // скипаем перенос строки, чтобы не читать его
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); 

    // читаем доску
    data.figures.resize(n);
    for (int i = 0; i < n; i++) {
        std::getline(std::cin, line);
        // если кривая строка (можно потом удалить?)
        if (line.size() < 6) continue;
        data.figures[i].pos.x = line[0];
        data.figures[i].pos.y = line[1];
        data.figures[i].color = line[3];
        data.figures[i].type = static_cast<FigureType>(line[5] - '0'); // переводим в число
    }

    // клетка взятия на проходе (если она есть)
    std::getline(std::cin, line);
    if (line.size() >= 2 && line[0] >= 'a' && line[0] <= 'h' && line[1] >= '1' && line[1] <= '8') {
        data.ep_square = {line[0], line[1]};
    } else {
        data.ep_square = {'\0', '\0'};      // если ее нет
    }

    // рокировка
    // сначала моя, потом врага; a = длинная, h = короткая
    // != '-' => true, == '-' => false
    std::getline(std::cin, line);
    std::istringstream iss(line);
    char my_a, my_h, opp_a, opp_h;
    if (iss >> my_a >> my_h >> opp_a >> opp_h) {
        data.castling[0] = (my_a != '-');
        data.castling[1] = (my_h != '-');
        data.castling[2] = (opp_a != '-');
        data.castling[3] = (opp_h != '-');
    }
}

// дебагер для нас
void debug(const std::string& msg) {
    std::cout << "# " << msg << '\n';
    std::cout.flush();
}

// отправляем ход серверу: клеткачисло, пробел и пинг с переходом на новую строку
void output_move(const Cell& from, const Cell& to, int ping) {
    std::cout << from.x << from.y << to.x << to.y << ' ' << ping << '\n';
    std::cout.flush();
}



class ChessBot {
private:
    OpeningBook opening;            // библиотека дебютов
    std::string move_history;       // история ходов через '_'

    // текущий ключ позиции
    std::string make_position_key() const {
        return move_history;
    }

    // совершаем ход из библиотеки дебютов: откуда -> кудась
    Move get_opening_move() {
        std::string key = make_position_key();
        std::string response = opening.lookup(key);
        if (!response.empty() && response.length() == 4) {
            Move m;
            m.from = {response[0], response[1]};
            m.to = {response[2], response[3]};
            debug("Opening book: " + response);
            return m;
        }
        return Move();
    }

public:
    // конструктор по умолчанию
    ChessBot() = default;
    // совершает ход 
    Move get_move(const InputData&) {
        Move m = get_opening_move();
        // добавляем в историю, если ход валиден
        if (m.valid()) {
            move_history += (move_history.empty() ? "" : "_") + m.str();
        }
        return m;
    }
    // добавляем ход соперника
    void add_opponent_move(const std::string& opp_move) {
        move_history += (move_history.empty() ? "" : "_") + opp_move;
    }
};

int main() {
    ChessBot bot;       // создаем ботика
    InputData in;       // данные от сервера

    // пока геймим
    while (true) {
        read_input(in);

        Move best = bot.get_move(in);                       // лучший ход бота ([CHECK] пока только дебюты)
        int next_ping = (in.pong<=0) ? 1 : (in.pong+1);     // увеличиваем по кд ping на 1
        if (best.valid()) {
            output_move(best.from, best.to, next_ping);
        } else {
            debug("Нет хода по дебюту");
        }
    }

    return 0;
}
