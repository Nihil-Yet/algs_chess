#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <unordered_map>
#include <limits>
#include <cstddef>

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
    // структура для дебага - ход и название дебюты [CHECK] - ПОТОМ УДАЛИТЬ!!
    struct BookMove {
        std::string move;
        std::string opening;
    };
    // хэээш :3333
    // история ходов - ключ; значения - варианты моих ходов из дебютной книги
    std::unordered_map<std::string, std::vector<BookMove>> book;
    // для дебага [CHECK] - ПОТОМ УДАЛИТЬ!!
    void add(const std::string& key, const std::string& move, const std::string& opening) {
        book[key].push_back({move, opening});
    }

public:
    OpeningBook() { load(); }

    // определение белые мы или черные происходит в начале игры, относительно того где стоит наш '+':
    // т.е., если в начале - мы белые; если в конце - мы черные
    // держим две библиотеки для дебютов за белых/черных
    void load() {

        // ДЛЯ БЕЛЫХ С E4!!!!!!!!!!!!!!!!!!!!!!!
        add("", "e2e4", "БЕЛЫЕ: старт 1.e4");

        // 1) Итальянская: e4 e5 Nf3 Nc6 Bc4
        add("e2e4_e7e5", "g1f3", "Итальянская партия");
        add("e2e4_e7e5_g1f3_b8c6", "f1c4", "Итальянская партия");
        add("e2e4_e7e5_g1f3_b8c6_f1c4_f8c5", "c2c3", "Итальянская партия");
        add("e2e4_e7e5_g1f3_b8c6_f1c4_f8c5_c2c3_g8f6", "d2d3", "Итальянская партия");

        // 2) Шотландская: e4 e5 Nf3 Nc6 d4
        add("e2e4_e7e5_g1f3_b8c6", "d2d4", "Шотландская партия");

        // 3) Венская: e4 e5 Nc3
        add("e2e4_e7e5", "b1c3", "Венская партия");

        // 4) Испанкая: e4 e5 Nf3 Nc6 Bb5 a6 Ba4
        add("e2e4_e7e5_g1f3_b8c6", "f1b5", "Испанская партия");
        add("e2e4_e7e5_g1f3_b8c6_f1b5_a7a6", "b5a4", "Испанская партия");

        // 5) Королевский гамбит: e4 e5 f4
        add("e2e4_e7e5", "f2f4", "Королевский гамбит");

        // 6) Открытая Сицилианка: e4 c5 Nf3 d6 d4 cxd4 Nxd4
        add("e2e4_c7c5", "g1f3", "Открытая Сицилианская");
        add("e2e4_c7c5_g1f3_d7d6", "d2d4", "Открытая Сицилианская");
        add("e2e4_c7c5_g1f3_d7d6_d2d4_c5d4", "f3d4", "Открытая Сицилианская");

        // 7) Алапин: e4 c5 c3
        add("e2e4_c7c5", "c2c3", "Алапин");

        // 8) Атака Гран-При: e4 c5 Nc3 Nc6 f4
        add("e2e4_c7c5", "b1c3", "Атака Гран-При");
        add("e2e4_c7c5_b1c3_b8c6", "f2f4", "Атака Гран-При");

        // 9) Московский/Россолимо-вариант: e4 c5 Nf3 Nc6 Bb5
        add("e2e4_c7c5", "g1f3", "Московский/Россолимо-вариант");
        add("e2e4_c7c5_g1f3_b8c6", "f1b5", "Московский/Россолимо-вариант");

        // 10) Французская: e4 e6 d4 d5 e5
        add("e2e4_e7e6", "d2d4", "Французская");
        add("e2e4_e7e6_d2d4_d7d5", "e4e5", "Французская");

        // 11) Французская Classical: e4 e6 d4 d5 Nc3
        add("e2e4_e7e6_d2d4_d7d5", "b1c3", "Французская классика");

        // 12) Французская Тарраш: e4 e6 d4 d5 Nd2
        add("e2e4_e7e6_d2d4_d7d5", "b1d2", "Французская Тарраша");

        // 13) Каро-Канн: e4 c6 d4 d5 e5
        add("e2e4_c7c6", "d2d4", "Каро-Канн");
        add("e2e4_c7c6_d2d4_d7d5", "e4e5", "Каро-Канн");

        // 14) Каро-Канн два коня: e4 c6 Nc3 d5 Nf3
        add("e2e4_c7c6", "b1c3", "Каро-Канн два коня");
        add("e2e4_c7c6_b1c3_d7d5", "g1f3", "Каро-Канн два коня");

        // ДЛЯ ЧЕРНЫХ ПРОТИВ E4/Е5!!!!!!!!!!!!!!!!!!!!!!!

        // 1) Против 1.e4: ...e5 (классика)
        add("e2e4", "e7e5", "Открытая игра за черных");

        // 2) Берлин против Испанки
        add("e2e4_e7e5_g1f3_b8c6_f1b5", "g8f6", "Берлинская защита");

        // 3) Каро-Канн
        add("e2e4", "c7c6", "Каро-Канн");
        add("e2e4_c7c6_d2d4", "d7d5", "Каро-Канн");

        // 4) Скандинавская
        add("e2e4", "d7d5", "Скандинавская защита");
        add("e2e4_d7d5_e4d5", "d8d5", "Скандинавская защита");

        // 5) Пирц
        add("e2e4", "d7d6", "Защита Пирца");
        add("e2e4_d7d6_d2d4", "g8f6", "Защита Пирца");
        add("e2e4_d7d6_d2d4_g8f6_b1c3", "g7g6", "Защита Пирца");

        // 6) Модерн-Бенони
        add("e2e4", "g7g6", "Модерн-Бенони");
        add("e2e4_g7g6_d2d4", "f8g7", "Модерн-Бенони");
        add("e2e4_g7g6_d2d4_f8g7_b1c3", "d7d6", "Модерн-Бенони");

        // 7) Французская
        add("e2e4", "e7e6", "Французская защита");
        add("e2e4_e7e6_d2d4", "d7d5", "Французская защита");

        // 8) Отказанный ферзевой гамбит против 1.d4
        add("d2d4", "d7d5", "Отказанный ферзевой гамбит");
        add("d2d4_d7d5_c2c4", "e7e6", "Отказанный ферзевой гамбит");

        // 9) Славянская
        add("d2d4", "d7d5", "Славянская защита");
        add("d2d4_d7d5_c2c4", "c7c6", "Славянская защита");

        // 10) Чебаненко-идея
        add("d2d4_d7d5_c2c4_c7c6_b1c3", "a7a6", "Славянская Чебаненко");

        // 11) Голландская
        add("d2d4", "f7f5", "Голландская защита");

        // 12) Защита Нимцовича
        add("d2d4", "g8f6", "Защита Нимцовича");
        add("d2d4_g8f6_c2c4", "e7e6", "Защита Нимцовича");
        add("d2d4_g8f6_c2c4_e7e6_b1c3", "f8b4", "Защита Нимцовича");

        // 13) Симметрия против Английской
        add("c2c4", "c7c5", "Английская симметрия");

        // 14) Универсальная против 1.Nf3
        add("g1f3", "d7d5", "Универсальная схема против Nf3");
        add("g1f3_d7d5", "g8f6", "Универсальная схема против Nf3");
        add("g1f3_d7d5_g8f6", "e7e6", "Универсальная схема против Nf3");
    }
    // имщем ключики
    BookMove lookup(const std::string& position_key, int variation_seed = 0) const {
        auto it = book.find(position_key);                              // хэшируем, ищем совпадение ключей
        if (it == book.end() || it->second.empty()) return {"", ""};    // если ключика нема
        const auto& vars = it->second;
        // выбирает какое продолжение выбрать, если несколько вариантов их одной точки путем остатка от деления номера хода на число вариантов
        std::size_t idx = static_cast<std::size_t>(variation_seed) % vars.size();
        return vars[idx];
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

// определяем сторону только по первому входному списку фигур:
// если в самом начале есть '+', то мы белые, иначе черные
bool detect_side(const InputData& in) {
    for (const auto& f : in.figures) {
        if (f.color == '+') return true;
    }
    return false;
}

// восстанавливает ход соперника (откуда фигура исчезла и куда появилась)
std::string figure_key(const Figure& f) {
    return std::string(1, f.pos.x) + f.pos.y;
}

// восстановления хода соперника, ищем откуда ушла фигура и куда пришла
std::string infer_opponent_move(const InputData& prev, const InputData& curr) {
    std::unordered_map<std::string, FigureType> prev_opp;       // предыдущий снимок доски
    std::unordered_map<std::string, FigureType> curr_opp;       // текущий снимок доски

    // заполняем предыдущим
    for (const auto& f : prev.figures) {
        if (f.color == '-') prev_opp[figure_key(f)] = f.type;
    }
    // заполняем текущим
    for (const auto& f : curr.figures) {
        if (f.color == '-') curr_opp[figure_key(f)] = f.type;
    }

    std::vector<std::string> from_opp;      // от
    std::vector<std::string> to_opp;        // до

    // откуда ушли
    for (const auto& it : prev_opp) {
        if (curr_opp.find(it.first) == curr_opp.end()) {
            from_opp.push_back(it.first);
        }
    }
    // куда пришли
    for (const auto& it : curr_opp) {
        if (prev_opp.find(it.first) == prev_opp.end()) {
            to_opp.push_back(it.first);
        }
    }

    // обычный ход, не рокировка
    if (from_opp.size() == 1 && to_opp.size() == 1) {
        return from_opp[0] + to_opp[0];
    }

    // рокировка соперника - либо длинная, либо короткая
    // откуда исчезли фигуры
    for (const auto& from : from_opp) {
        if (from != "e1" && from != "e8") continue;     // варианты только с e1 по e8
        // где фигуры появились
        for (const auto& to : to_opp) {
            // рокировочные места
            if (to == "g1" || to == "c1" || to == "g8" || to == "c8") {
                return from + to;       // ход короля, ладью не записываем
            }
        }
    }

    return "";
}



class ChessBot {
private:
    OpeningBook opening;            // библиотека дебютов
    std::string move_history;       // история ходов через '_'
    bool bot_is_white = true;       // сторона изначально считается белой

    // текущий ключ позиции
    std::string make_position_key() const {
        return move_history;
    }

    // совершаем ход из библиотеки дебютов: откуда -> кудась
    Move get_opening_move(int variation_seed) {
        std::string key = make_position_key();
        auto response = opening.lookup(key, variation_seed);
        const std::string& move = response.move;
        const std::string& opening_name = response.opening;
        if (!move.empty() && move.length() == 4) {
            Move m;
            m.from = {move[0], move[1]};
            m.to = {move[2], move[3]};
            // [CHECK] - ДЕБАГ ДЕБЮТОВ, ШОБ ЗНАТЬ КАКОЙ СЕЙЧАС РАБОТАЕТ. ПОТОМ УДАЛИТЬ
            debug("Дебют: " + opening_name + " | ход: " + move);
            return m;
        }
        return Move();
    }

public:
    // конструктор по умолчанию
    ChessBot() = default;
    // устанавливаем сторону
    void set_side(bool is_white) {
        bot_is_white = is_white;
    }
    // совершает ход 
    Move get_move(const InputData& in) {
        // если мы черные и еще нет хода соперника в истории, то пока нечего играть
        if (!bot_is_white && move_history.empty()) return Move();

        Move m = get_opening_move(in.pong);
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
    InputData prev;     // предыдущий кадр доски, чтобы понять ход соперника
    bool has_prev = false;
    bool side_known = false;

    // пока геймим
    while (true) {
        read_input(in);

        // определяем сторону бота один раз
        if (!side_known && in.count > 0) {
            bool is_white = detect_side(in);
            bot.set_side(is_white);
            side_known = true;
            debug(std::string("Сторона бота: ") + (is_white ? "white" : "black"));
        }

        // добавляем ход соперника в историю
        if (has_prev) {
            std::string opp = infer_opponent_move(prev, in);
            if (!opp.empty()) {
                bot.add_opponent_move(opp);
                debug("Ход соперника: " + opp);
            }
        }

        Move best = bot.get_move(in);                       // лучший ход бота ([CHECK] пока только дебюты)
        int next_ping = (in.pong<=0) ? 1 : (in.pong+1);     // увеличиваем по кд ping на 1
        if (best.valid()) {
            output_move(best.from, best.to, next_ping);
        } else {
            debug("Нет хода по дебюту");
        }

        prev = in;          // сохраняем текущую, как предыдущую для следующего
        has_prev = true;    // есть предыдущий
    }

    return 0;
}
