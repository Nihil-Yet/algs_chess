#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <unordered_map>
#include <map>
#include <deque>
#include <cstddef>
#include <chrono>
#include <algorithm>

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
bool G_BOT_IS_WHITE = true; // сторона бота для направления пешек

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
public:
    // запись из библиотеки: какой ход и как называется дебют
    struct BookMove {
        std::string move;
        std::string opening;
    };

private:
    // раздельные библиотеки по стороне
    std::unordered_map<std::string, std::vector<BookMove>> white_book;
    std::unordered_map<std::string, std::vector<BookMove>> black_book;
    
    // те же библиотеки, но ключ уже хэш позиции
    std::unordered_map<std::string, std::vector<BookMove>> white_hash_book;
    std::unordered_map<std::string, std::vector<BookMove>> black_hash_book;


    bool loading_white = true;
    using Board = std::map<std::string, std::pair<char, int>>; // клетка -> (цвет, тип)

    // берем вариант из списка по индексу
    static BookMove pick_from_bucket(const std::vector<BookMove>& vars, int variation_seed) {
        if (vars.empty()) return {"", ""};
        std::size_t idx = static_cast<std::size_t>(variation_seed) % vars.size();
        return vars[idx];
    }
    // добавляем ход в нужную библиотеку
    void add(const std::string& key, const std::string& move, const std::string& opening) {
        auto& target = loading_white ? white_book : black_book;
        target[key].push_back({move, opening});
    }

    // разбиваем history key по "_"
    static std::vector<std::string> split_key(const std::string& key) {
        std::vector<std::string> out;
        if (key.empty()) return out;
        std::stringstream ss(key);    // поток, чтобы удобно резать строку по "_"
        std::string item;             // текущий кусок между "_"
        while (std::getline(ss, item, '_')) {
            if (!item.empty()) out.push_back(item);
        }
        return out;
    }

    // доска для симуляции истории
    static Board start_board(bool bot_white) {
        Board b;    // карта позиции: "клетка -> (цвет, тип)"
        auto put = [&](const std::string& sq, char c, int t) { b[sq] = {c, t}; };   // спидран по карте

        char bottom = bot_white ? '+' : '-';    // кто стоит снизу 
        put("a1", bottom, 4); put("b1", bottom, 2); put("c1", bottom, 3); put("d1", bottom, 5);     // крутые фигуры
        put("e1", bottom, 6); put("f1", bottom, 3); put("g1", bottom, 2); put("h1", bottom, 4);     // лоховские пешки какие-то
        // оп-оп, досочка
        for (char f = 'a'; f <= 'h'; f++) {
            std::string sq; sq += f; sq += '2';     // крутые ребята
            put(sq, bottom, 1);                     // пешки
        }

        char top = bot_white ? '-' : '+';    // кто стоит сверху 
        put("a8", top, 4); put("b8", top, 2); put("c8", top, 3); put("d8", top, 5);     // крутые фигуры
        put("e8", top, 6); put("f8", top, 3); put("g8", top, 2); put("h8", top, 4);     // лоховские пешки какие-то
        for (char f = 'a'; f <= 'h'; f++) {
            std::string sq; sq += f; sq += '7';     // крутые ребята
            put(sq, top, 1);                        // пешки
        }
        return b;
    }

    // применяем ход на доске
    static bool apply_sim_move(Board& b, const std::string& mv, char expected_color) {
        if (mv.size() != 4) return false;
        std::string from = mv.substr(0, 2);
        std::string to = mv.substr(2, 2);
        auto it = b.find(from);     // ищем фигуру на ОТ
        if (it == b.end()) return false;
        if (it->second.first != expected_color) return false;
        auto piece = it->second;
        b.erase(it);                                  // ОТ
        b[to] = piece;                                // ДО
        return true;
    }

    // хэш позиции на доске (одна функция для симуляции и для реального входа)
    static std::string hash_board(const Board& b) {
        std::string h;
        for (const auto& it : b) {
            h += it.first;                     // клетка, например "e4"
            h += it.second.first;              // цвет
            h += char('0' + it.second.second); // тип
            h += ';';                          // разделитель записи
        }
        return h;
    }

    // хэш реальной позиции
    static std::string hash_input_board(const InputData& in) {
        std::map<std::string, std::pair<char, int>> b;
        for (const auto& f : in.figures) {
            std::string sq; sq += f.pos.x; sq += f.pos.y;
            b[sq] = {f.color, static_cast<int>(f.type)};    // собираем позицию из входа сервера
        }
        return hash_board(b);
    }

    // строим хэш из истории ключей нащей библиотеки
    void build_hash_book(bool for_white) {
        const auto& src = for_white ? white_book : black_book;
        auto& dst = for_white ? white_hash_book : black_hash_book;
        dst.clear();

        for (const auto& kv : src) {
            const std::string& key = kv.first;
            const auto& responses = kv.second;     // ответы для этого ключа
            Board b = start_board(for_white);
            auto plies = split_key(key);
            char mover = for_white ? '+' : '-';    // кто ходит первым
            bool ok = true;

            for (const auto& mv : plies) {
                if (!apply_sim_move(b, mv, mover)) { ok = false; break; }
                mover = (mover == '+') ? '-' : '+';     // смена хода
            }
            if (!ok) continue;

            std::string pos_hash = hash_board(b);                   // хэш позиции
            auto& bucket = dst[pos_hash];
            for (const auto& bm : responses) bucket.push_back(bm); // переносим варианты
        }
    }

public:
    OpeningBook() { load(); }

    // загружаем библиотеки дебютов: сначала белых, потом черных
    void load() {
        loading_white = true;

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
        loading_white = false;

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

        // после загрузки строковых ключей строим хэш-книги позиций
        build_hash_book(true);
        build_hash_book(false);
    }
    // lookup по ключу истории
    BookMove lookup(bool is_white, const std::string& position_key, int variation_seed = 0) const {
        const auto& target = is_white ? white_book : black_book;        // смотрим какую библиотеку юзаем
        auto it = target.find(position_key);                            // хэшируем, ищем совпадение ключей
        if (it == target.end()) return {"", ""};                        // если ключика нема
        return pick_from_bucket(it->second, variation_seed);
    }

    // по хэшу текущей позиции
    BookMove lookup_by_hash(bool is_white, const InputData& in, int variation_seed = 0) const {
        const auto& target = is_white ? white_hash_book : black_hash_book;
        std::string h = hash_input_board(in);
        auto it = target.find(h);
        if (it == target.end()) return {"", ""};
        return pick_from_bucket(it->second, variation_seed);
    }
};



// чтение ответа сервера
bool read_input(InputData& data) {
    // читаем pong
    if (!(std::cin >> data.pong)) return false;

    // получаем кол-во фигур
    std::cin >> data.count;

    // читаем доску
    data.figures.resize(data.count);
    for (auto& fig : data.figures) {
        int type = 0;
        std::cin >> fig.pos.x >> fig.pos.y >> fig.color >> type;
        fig.type = static_cast<FigureType>(type);
    }

    // клетка взятия на проходе
    std::string ep;
    std::cin >> ep;
    if (ep.size() == 2 &&
        ep[0] >= 'a' && ep[0] <= 'h' &&
        ep[1] >= '1' && ep[1] <= '8') {
        data.ep_square = {ep[0], ep[1]};
    } else {
        data.ep_square = {};
    }

    // рокировка
    // сначала моя, потом врага; a = длинная, h = короткая
    // != '-' => true, == '-' => false
    char my_a, my_h, opp_a, opp_h;
    std::cin >> my_a >> my_h >> opp_a >> opp_h;
    data.castling[0] = (my_a != '-');
    data.castling[1] = (my_h != '-');
    data.castling[2] = (opp_a != '-');
    data.castling[3] = (opp_h != '-');
    return true;
}

// отправляем ход серверу: клеткачисло, пробел и пинг с переходом на новую строку
void output_move(const Cell& from, const Cell& to, int ping) {
    std::cout << from.x << from.y << to.x << to.y << ' ' << ping << '\n';
    std::cout.flush();
}

// определяем сторону только по первому входному списку фигур:
// сначала пробуем по пешкам, потом fallback по королю
bool detect_side(const InputData& in) {
    for (const auto& f : in.figures) {
        if (f.color != '+') continue;
        if (f.type == FigureType::KING) {
            if (f.pos.y == '1') return true;
            if (f.pos.y == '8') return false;
        }
    }

    int plus_pawn_on2 = 0, plus_pawn_on7 = 0;
    int minus_pawn_on2 = 0, minus_pawn_on7 = 0;
    int plus_pawn_sum = 0, plus_pawn_cnt = 0;
    int minus_pawn_sum = 0, minus_pawn_cnt = 0;

    for (const auto& f : in.figures) {
        if (f.type != FigureType::PAWN) continue;
        if (f.color == '+') {
            if (f.pos.y == '2') plus_pawn_on2++;
            if (f.pos.y == '7') plus_pawn_on7++;
        } else {
            if (f.pos.y == '2') minus_pawn_on2++;
            if (f.pos.y == '7') minus_pawn_on7++;
        }
        int ry = f.pos.y - '0';
        if (f.color == '+') { plus_pawn_sum += ry; plus_pawn_cnt++; }
        else { minus_pawn_sum += ry; minus_pawn_cnt++; }
    }

    int white_like = plus_pawn_on2 + minus_pawn_on7;
    int black_like = plus_pawn_on7 + minus_pawn_on2;
    if (white_like != black_like) return white_like > black_like;

    // у белых обычно наши пешки "ниже" в координатах, чем вражеские
    if (plus_pawn_cnt > 0 && minus_pawn_cnt > 0) {
        return (plus_pawn_sum * minus_pawn_cnt) < (minus_pawn_sum * plus_pawn_cnt);
    }

    // fallback по королю
    for (const auto& f : in.figures) {
        if (f.color == '+' && f.type == FigureType::KING) {
            return (f.pos.y <= '4');
        }
    }

    return true;
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

// карта доски для мидгейма: ячейка - цвет, тип
using BoardMap = std::map<std::string, std::pair<char, FigureType>>;
using PieceEntry = std::pair<char, FigureType>;

const PieceEntry* find_piece_entry(const BoardMap& b, char x, char y) {
    std::string sq; sq += x; sq += y;
    auto it = b.find(sq);
    return (it == b.end()) ? nullptr : &it->second;
}

// собрали карту из входных фигур
BoardMap make_board_map(const InputData& in) {
    BoardMap b;
    for (const auto& f : in.figures) {
        b[f.pos.str()] = {f.color, f.type};
    }
    return b;
}

// клетка внутри доски?
bool on_board(char x, char y) {
    return x >= 'a' && x <= 'h' && y >= '1' && y <= '8';
}

// есть ли фигура на клетке?
bool has_piece(const BoardMap& b, char x, char y) {
    std::string sq; sq += x; sq += y;
    return b.find(sq) != b.end();
}

// цвет фигуры на клетке
char piece_color(const BoardMap& b, char x, char y) {
    std::string sq; sq += x; sq += y;
    auto it = b.find(sq);
    return (it == b.end()) ? '\0' : it->second.first;
}

// тип фигуры на клетке (если пусто - вернем пешку как заглушку)
FigureType piece_type(const BoardMap& b, char x, char y) {
    std::string sq; sq += x; sq += y;
    auto it = b.find(sq);
    return (it == b.end()) ? FigureType::PAWN : it->second.second;
}

// направление пешек по стороне бота (а не по позиции короля)
int pawn_dir_for_color(const BoardMap& b, char color) {
    (void)b;
    if (color == '+') return G_BOT_IS_WHITE ? 1 : -1;
    return G_BOT_IS_WHITE ? -1 : 1;
}

// бьется ли клетка фигурами нужного цвета
bool is_square_attacked(const BoardMap& b, char tx, char ty, char by_color) {
    int pawn_dir = pawn_dir_for_color(b, by_color);
    int pawn_from_step = -pawn_dir;

    // пешки
    for (int dx : {-1, 1}) {
        char px = static_cast<char>(tx + dx);
        char py = static_cast<char>(ty + pawn_from_step);
        if (!on_board(px, py)) continue;
        if (piece_color(b, px, py) == by_color && piece_type(b, px, py) == FigureType::PAWN) return true;
    }

    // кони
    static const int ndx[8] = {1, 2, 2, 1, -1, -2, -2, -1};
    static const int ndy[8] = {2, 1, -1, -2, -2, -1, 1, 2};
    for (int i = 0; i < 8; i++) {
        char x = static_cast<char>(tx + ndx[i]);
        char y = static_cast<char>(ty + ndy[i]);
        if (!on_board(x, y)) continue;
        if (piece_color(b, x, y) == by_color && piece_type(b, x, y) == FigureType::KNIGHT) return true;
    }

    // король рядом
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            if (dx == 0 && dy == 0) continue;
            char x = static_cast<char>(tx + dx);
            char y = static_cast<char>(ty + dy);
            if (!on_board(x, y)) continue;
            if (piece_color(b, x, y) == by_color && piece_type(b, x, y) == FigureType::KING) return true;
        }
    }

    // диагонали: слон/ферзь
    static const int bdx[4] = {1, 1, -1, -1};
    static const int bdy[4] = {1, -1, 1, -1};
    for (int i = 0; i < 4; i++) {
        char x = tx, y = ty;
        while (true) {
            x = static_cast<char>(x + bdx[i]);
            y = static_cast<char>(y + bdy[i]);
            if (!on_board(x, y)) break;
            char c = piece_color(b, x, y);
            if (c == '\0') continue;
            if (c == by_color) {
                FigureType t = piece_type(b, x, y);
                if (t == FigureType::BISHOP || t == FigureType::QUEEN) return true;
            }
            break;
        }
    }

    // прямые: ладья/ферзь
    static const int rdx[4] = {1, -1, 0, 0};
    static const int rdy[4] = {0, 0, 1, -1};
    for (int i = 0; i < 4; i++) {
        char x = tx, y = ty;
        while (true) {
            x = static_cast<char>(x + rdx[i]);
            y = static_cast<char>(y + rdy[i]);
            if (!on_board(x, y)) break;
            char c = piece_color(b, x, y);
            if (c == '\0') continue;
            if (c == by_color) {
                FigureType t = piece_type(b, x, y);
                if (t == FigureType::ROOK || t == FigureType::QUEEN) return true;
            }
            break;
        }
    }

    return false;
}

// добавляем ход, если клетка пустая/вражеская
void add_if_can_go(const BoardMap& b, std::vector<Move>& out, const Cell& from, char tx, char ty, char my_color) {
    if (!on_board(tx, ty)) return;
    char c = piece_color(b, tx, ty);
    if (c == my_color) return;               // свою бить нельзя
    out.push_back({from, {tx, ty}});         // пусто или враг - можно
}

// ходы коня (буква Г)
void gen_knight_moves(const BoardMap& b, std::vector<Move>& out, const Cell& from, char my_color) {
    static const int dx[8] = {1, 2, 2, 1, -1, -2, -2, -1};
    static const int dy[8] = {2, 1, -1, -2, -2, -1, 1, 2};
    for (int i = 0; i < 8; i++) {
        add_if_can_go(b, out, from, static_cast<char>(from.x + dx[i]), static_cast<char>(from.y + dy[i]), my_color);
    }
}

// ходы короля (на 1 клетку + рокировка)
void gen_king_moves(const BoardMap& b, std::vector<Move>& out, const Cell& from, char my_color, bool can_long, bool can_short) {
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            if (dx == 0 && dy == 0) continue;
            add_if_can_go(b, out, from, static_cast<char>(from.x + dx), static_cast<char>(from.y + dy), my_color);
        }
    }

    char enemy = (my_color == '+') ? '-' : '+';

    // длинная рокировка: e -> c
    if (can_long && from.x == 'e') {
        char y = from.y;
        if (!has_piece(b, 'd', y) && !has_piece(b, 'c', y) && !has_piece(b, 'b', y) &&
            piece_color(b, 'a', y) == my_color && piece_type(b, 'a', y) == FigureType::ROOK) {
            if (!is_square_attacked(b, 'e', y, enemy) &&
                !is_square_attacked(b, 'd', y, enemy) &&
                !is_square_attacked(b, 'c', y, enemy)) {
                out.push_back({from, {'c', y}});
            }
        }
    }

    // короткая рокировка: e -> g
    if (can_short && from.x == 'e') {
        char y = from.y;
        if (!has_piece(b, 'f', y) && !has_piece(b, 'g', y) &&
            piece_color(b, 'h', y) == my_color && piece_type(b, 'h', y) == FigureType::ROOK) {
            if (!is_square_attacked(b, 'e', y, enemy) &&
                !is_square_attacked(b, 'f', y, enemy) &&
                !is_square_attacked(b, 'g', y, enemy)) {
                out.push_back({from, {'g', y}});
            }
        }
    }
}

// слон/ладья/ферзь
void gen_slider_moves(const BoardMap& b, std::vector<Move>& out, const Cell& from, char my_color, const int* dx, const int* dy, int n) {
    for (int i = 0; i < n; i++) {
        char x = from.x;
        char y = from.y;
        while (true) {
            x = static_cast<char>(x + dx[i]);
            y = static_cast<char>(y + dy[i]);
            if (!on_board(x, y)) break;
            char c = piece_color(b, x, y);
            if (c == my_color) break;           // уперлись в свою
            out.push_back({from, {x, y}});      // пусто или взятие
            if (c != '\0') break;               // после взятия дальше нельзя
        }
    }
}

// ходы пешки
void gen_pawn_moves(const BoardMap& b, std::vector<Move>& out, const Cell& from, char my_color, const Cell& ep_square) {
    int dir = pawn_dir_for_color(b, my_color);
    char one_y = static_cast<char>(from.y + dir);
    char two_y = static_cast<char>(from.y + 2 * dir);
    char start_y = '2';
    if (G_BOT_IS_WHITE) {
        start_y = (my_color == '+') ? '2' : '7';
    } else {
        start_y = (my_color == '+') ? '7' : '2';
    }
    char enemy_color = (my_color == '+') ? '-' : '+';
    char promotion_y = (dir > 0) ? '8' : '1';

    // [CHECK] сервер ругается на fromto-ходы на последнюю горизонталь (промоушен), поэтому не шлем их
    if (on_board(from.x, one_y) && one_y != promotion_y && !has_piece(b, from.x, one_y)) {
        out.push_back({from, {from.x, one_y}});
        // шаг на 2 со старта
        if (from.y == start_y && on_board(from.x, two_y) && !has_piece(b, from.x, two_y)) {
            out.push_back({from, {from.x, two_y}});
        }
    }

    // взятия по диагонали
    for (int dx : {-1, 1}) {
        char tx = static_cast<char>(from.x + dx);
        char ty = one_y;
        if (!on_board(tx, ty)) continue;
        if (ty == promotion_y) continue;
        if (piece_color(b, tx, ty) == enemy_color) {
            out.push_back({from, {tx, ty}});
        }
    }

    // взятие на проходе
    if (ep_square.valid()) {
        if (one_y == ep_square.y && (from.x + 1 == ep_square.x || from.x - 1 == ep_square.x)) {
            out.push_back({from, ep_square});
        }
    }
}

// ходы одной фигуры по ее типу
void generate_piece_moves(const Figure& f, const BoardMap& b, const InputData& in, std::vector<Move>& out) {
    static const int bdx[4] = {1, 1, -1, -1};
    static const int bdy[4] = {1, -1, 1, -1};
    static const int rdx[4] = {1, -1, 0, 0};
    static const int rdy[4] = {0, 0, 1, -1};
    static const int qdx[8] = {1, 1, -1, -1, 1, -1, 0, 0};
    static const int qdy[8] = {1, -1, 1, -1, 0, 0, 1, -1};
    if (f.type == FigureType::PAWN) {
        gen_pawn_moves(b, out, f.pos, f.color, in.ep_square);
    } else if (f.type == FigureType::KNIGHT) {
        gen_knight_moves(b, out, f.pos, f.color);
    } else if (f.type == FigureType::BISHOP) {
        gen_slider_moves(b, out, f.pos, f.color, bdx, bdy, 4);
    } else if (f.type == FigureType::ROOK) {
        gen_slider_moves(b, out, f.pos, f.color, rdx, rdy, 4);
    } else if (f.type == FigureType::QUEEN) {
        gen_slider_moves(b, out, f.pos, f.color, qdx, qdy, 8);
    } else if (f.type == FigureType::KING) {
        bool can_long = (f.color == '+') ? in.castling[0] : in.castling[2];
        bool can_short = (f.color == '+') ? in.castling[1] : in.castling[3];
        gen_king_moves(b, out, f.pos, f.color, can_long, can_short);
    }
}

// применяем ход на копии доски (надо для фильтра легальности)
BoardMap apply_move_copy(const BoardMap& b, const Move& m) {
    BoardMap nb = b;
    std::string from = m.from.str();
    std::string to = m.to.str();
    auto it = nb.find(from);
    if (it == nb.end()) return nb;

    char mover_color = it->second.first;
    FigureType mover_type = it->second.second;

    // en passant: пешка по диагонали на пустую клетку
    if (mover_type == FigureType::PAWN && m.from.x != m.to.x && nb.find(to) == nb.end()) {
        int dir = pawn_dir_for_color(nb, mover_color);
        char cap_y = static_cast<char>(m.to.y - dir);
        std::string cap_sq; cap_sq += m.to.x; cap_sq += cap_y;
        nb.erase(cap_sq);
    }

    // рокировка: двигаем ладью
    if (mover_type == FigureType::KING && m.from.x == 'e' && (m.to.x == 'g' || m.to.x == 'c')) {
        if (m.to.x == 'g') {
            std::string rook_from = std::string("h") + m.from.y;
            std::string rook_to = std::string("f") + m.from.y;
            auto rt = nb.find(rook_from);
            if (rt != nb.end()) {
                auto rook_piece = rt->second;
                nb.erase(rt);
                nb[rook_to] = rook_piece;
            }
        } else {
            std::string rook_from = std::string("a") + m.from.y;
            std::string rook_to = std::string("d") + m.from.y;
            auto rt = nb.find(rook_from);
            if (rt != nb.end()) {
                auto rook_piece = rt->second;
                nb.erase(rt);
                nb[rook_to] = rook_piece;
            }
        }
    }

    auto piece = it->second;
    nb.erase(it);
    nb[to] = piece;
    return nb;
}

// где стоит наш король
Cell find_king_cell(const BoardMap& b, char my_color) {
    for (const auto& it : b) {
        if (it.second.first == my_color && it.second.second == FigureType::KING) {
            return {it.first[0], it.first[1]};
        }
    }
    return {};
}

// все псевдолегальные ходы нужного цвета
std::vector<Move> generate_all_moves_color(const InputData& in, const BoardMap& b, char color) {
    std::vector<Move> all;
    all.reserve(64);
    for (const auto& f : in.figures) {
        if (f.color != color) continue;
        generate_piece_moves(f, b, in, all);
    }
    return all;
}

// фильтр легальности для любого цвета
std::vector<Move> filter_legal_moves_color(const BoardMap& b, const std::vector<Move>& pseudo, char color) {
    std::vector<Move> legal;
    legal.reserve(pseudo.size());
    char enemy = (color == '+') ? '-' : '+';
    for (const auto& m : pseudo) {
        BoardMap nb = apply_move_copy(b, m);
        Cell king = find_king_cell(nb, color);
        if (!king.valid()) continue;
        if (!is_square_attacked(nb, king.x, king.y, enemy)) legal.push_back(m);
    }
    return legal;
}

std::vector<Move> generate_all_legal_moves_color(const InputData& in, char color) {
    BoardMap b = make_board_map(in);
    auto pseudo = generate_all_moves_color(in, b, color);
    return filter_legal_moves_color(b, pseudo, color);
}

// вес фигуры по типу (удобно для оценки взятий)
int piece_weight(FigureType t) {
    return FIGURE_WEIGHTS[static_cast<int>(t)];
}

int manhattan(const Cell& a, const Cell& b) {
    int dx = a.x - b.x; if (dx < 0) dx = -dx;
    int dy = a.y - b.y; if (dy < 0) dy = -dy;
    return dx + dy;
}

int file_dist_to_center(char x) {
    int d = x - 'd';
    if (d < 0) d = -d;
    int e = x - 'e';
    if (e < 0) e = -e;
    return (d < e) ? d : e;
}

int rank_dist_to_center(char y) {
    int d = y - '4';
    if (d < 0) d = -d;
    int e = y - '5';
    if (e < 0) e = -e;
    return (d < e) ? d : e;
}

int king_center_bonus(const Cell& k) {
    return 8 * (6 - file_dist_to_center(k.x) - rank_dist_to_center(k.y));
}

bool is_opening_phase(const InputData& in) {
    return in.count >= 26;
}

char our_home_rank() {
    return G_BOT_IS_WHITE ? '1' : '8';
}

bool is_castling_move(const Move& m) {
    return m.from.x == 'e' && (m.to.x == 'g' || m.to.x == 'c') && m.from.y == m.to.y;
}

int center_square_bonus(char x, char y) {
    if ((x == 'd' || x == 'e') && (y == '4' || y == '5')) return 18;
    if ((x == 'c' || x == 'd' || x == 'e' || x == 'f') && (y >= '3' && y <= '6')) return 10;
    return 0;
}

bool is_capture_move_on_board(const BoardMap& b, const Move& m, char side) {
    const PieceEntry* target = find_piece_entry(b, m.to.x, m.to.y);
    if (target && target->first != side) return true;
    const PieceEntry* mover = find_piece_entry(b, m.from.x, m.from.y);
    if (!mover || mover->first != side) return false;
    return mover->second == FigureType::PAWN && m.from.x != m.to.x && !target;
}

bool is_progress_move_on_board(const BoardMap& b, const Move& m, char side) {
    const PieceEntry* mover = find_piece_entry(b, m.from.x, m.from.y);
    if (!mover || mover->first != side) return false;
    if (mover->second == FigureType::PAWN) return true;
    return is_capture_move_on_board(b, m, side);
}

bool is_in_check_on_board(const BoardMap& b, char side) {
    Cell king = find_king_cell(b, side);
    if (!king.valid()) return false;
    char enemy = (side == '+') ? '-' : '+';
    return is_square_attacked(b, king.x, king.y, enemy);
}

int score_move_ordering(const BoardMap& b, const Move& m, bool opening_bias) {
    int score = 0;
    const PieceEntry* mover = find_piece_entry(b, m.from.x, m.from.y);
    const PieceEntry* captured = find_piece_entry(b, m.to.x, m.to.y);

    if (captured && captured->first == '-') {
        int cap = piece_weight(captured->second) * 45;
        if (captured->second == FigureType::QUEEN) cap = 180;
        score += cap;
    }

    score += center_square_bonus(m.to.x, m.to.y);

    if (mover) {
        if (mover->second == FigureType::PAWN) {
            int dir = G_BOT_IS_WHITE ? 1 : -1;
            int dy = m.to.y - m.from.y;
            if (dy == dir || dy == 2 * dir) score += 8;
            if (m.to.x == 'c' || m.to.x == 'd' || m.to.x == 'e' || m.to.x == 'f') score += 8;
            if (m.to.x == 'a' || m.to.x == 'b' || m.to.x == 'g' || m.to.x == 'h') score += 4;
            int steps_to_promo = (dir > 0) ? ('8' - m.to.y) : (m.to.y - '1');
            if (steps_to_promo == 1) score += 180;
            else if (steps_to_promo == 2) score += 90;
        }

        if ((mover->second == FigureType::KNIGHT || mover->second == FigureType::BISHOP) &&
            m.from.y == our_home_rank() && m.to.y != our_home_rank()) {
            score += 16;
        }

        if (opening_bias && mover->second == FigureType::QUEEN && m.from.y == our_home_rank()) {
            score -= 55;
        }
        if (opening_bias && mover->second == FigureType::ROOK && m.from.y == our_home_rank()) {
            score -= 30;
        }
        if (opening_bias && mover->second == FigureType::KING && !is_castling_move(m)) {
            score -= 60;
        }
        if (mover->second == FigureType::KING && is_castling_move(m)) {
            score += 30;
        }

        BoardMap nb = apply_move_copy(b, m);
        if (is_square_attacked(nb, m.to.x, m.to.y, '-')) {
            int w = piece_weight(mover->second);
            if (w >= 9) score -= 220;
            else if (w >= 5) score -= 130;
            else if (w >= 3) score -= 55;
            else score -= 20;
        }
    }

    return score;
}

InputData make_next_state(const InputData& in, const BoardMap& b, const Move& m) {
    InputData out = in;
    BoardMap nb = apply_move_copy(b, m);

    out.figures.clear();
    out.figures.reserve(nb.size());
    for (const auto& it : nb) {
        Figure f;
        f.pos = {it.first[0], it.first[1]};
        f.color = it.second.first;
        f.type = it.second.second;
        out.figures.push_back(f);
    }
    out.count = static_cast<int>(out.figures.size());
    out.ep_square = {};
    out.castling[0] = out.castling[1] = out.castling[2] = out.castling[3] = false; // [CHECK] позже можно точнее
    return out;
}

// делаем следующее состояние после хода (с упрощениями для мидгейма)
InputData make_next_state(const InputData& in, const Move& m) {
    BoardMap b = make_board_map(in);
    return make_next_state(in, b, m);
}

// мало ли фигуры кроме пешек и королей -> это конец партии
bool is_endgame_position(const InputData& in) {
    int total_non_pawn = 0;
    for (const auto& f : in.figures) {
        if (f.type == FigureType::KING || f.type == FigureType::PAWN) continue;
        total_non_pawn += piece_weight(f.type);
    }
    return total_non_pawn <= 14; // порог из help/эндшпиль.md
}

bool is_passed_pawn(const BoardMap& b, const Figure& p) {
    int dir = pawn_dir_for_color(b, p.color);
    char enemy = (p.color == '+') ? '-' : '+';
    for (char fx = static_cast<char>(p.pos.x - 1); fx <= static_cast<char>(p.pos.x + 1); fx++) {
        if (fx < 'a' || fx > 'h') continue;
        char y = static_cast<char>(p.pos.y + dir);
        while (y >= '1' && y <= '8') {
            if (piece_color(b, fx, y) == enemy && piece_type(b, fx, y) == FigureType::PAWN) return false;
            y = static_cast<char>(y + dir);
        }
    }
    return true;
}

int passed_pawn_bonus(const Figure& p) {
    int rank = p.pos.y - '0';
    int advance = 0;
    if (G_BOT_IS_WHITE) {
        advance = (p.color == '+') ? (rank - 2) : (7 - rank);
    } else {
        advance = (p.color == '+') ? (7 - rank) : (rank - 2);
    }
    if (advance <= 0) return 0;
    if (advance == 1) return 12;
    if (advance == 2) return 20;
    if (advance == 3) return 35;
    if (advance == 4) return 55;
    return 80;
}

bool pawn_is_isolated(const BoardMap& b, const Figure& p) {
    for (int df : {-1, 1}) {
        char fx = static_cast<char>(p.pos.x + df);
        if (fx < 'a' || fx > 'h') continue;
        for (char y = '1'; y <= '8'; y++) {
            if (piece_color(b, fx, y) == p.color && piece_type(b, fx, y) == FigureType::PAWN) return false;
        }
    }
    return true;
}

bool pawn_is_doubled(const BoardMap& b, const Figure& p) {
    for (char y = '1'; y <= '8'; y++) {
        if (y == p.pos.y) continue;
        if (piece_color(b, p.pos.x, y) == p.color && piece_type(b, p.pos.x, y) == FigureType::PAWN) return true;
    }
    return false;
}

int square_rule_bonus(const Figure& p, const Cell& enemy_king) {
    int steps = 0;
    char promo_rank = '8';
    if (G_BOT_IS_WHITE) {
        steps = (p.color == '+') ? ('8' - p.pos.y) : (p.pos.y - '1');
        promo_rank = (p.color == '+') ? '8' : '1';
    } else {
        steps = (p.color == '+') ? (p.pos.y - '1') : ('8' - p.pos.y);
        promo_rank = (p.color == '+') ? '1' : '8';
    }
    if (steps <= 0) return 0;
    Cell promo = {p.pos.x, promo_rank};
    int king_dist = manhattan(enemy_king, promo);
    if (king_dist > steps) return 80; // король не в квадрате пешки
    return 0;
}

// обычная оценка (мидгейм)
int evaluate_midgame(const InputData& in, const BoardMap& b) {
    int score = 0;
    for (const auto& f : in.figures) {
        int w = piece_weight(f.type) * 100;
        score += (f.color == '+') ? w : -w;
    }

    Cell my_king = find_king_cell(b, '+');
    Cell opp_king = find_king_cell(b, '-');
    if (my_king.valid() && is_square_attacked(b, my_king.x, my_king.y, '-')) score -= 120;
    if (opp_king.valid() && is_square_attacked(b, opp_king.x, opp_king.y, '+')) score += 120;

    return score;
}

// отдельная оценка эндшпиля
int evaluate_endgame(const InputData& in, const BoardMap& b) {
    int score = 0;
    Cell my_king = find_king_cell(b, '+');
    Cell opp_king = find_king_cell(b, '-');

    // материал
    for (const auto& f : in.figures) {
        int w = piece_weight(f.type) * 100;
        score += (f.color == '+') ? w : -w;
    }

    // король в центре = хорошо
    if (my_king.valid()) score += king_center_bonus(my_king);
    if (opp_king.valid()) score -= king_center_bonus(opp_king);

    // пешечная структура + проходные + квадрат
    for (const auto& f : in.figures) {
        if (f.type != FigureType::PAWN) continue;
        int sign = (f.color == '+') ? 1 : -1;

        if (pawn_is_isolated(b, f)) score += sign * (-10);
        if (pawn_is_doubled(b, f)) score += sign * (-12);
        if (is_passed_pawn(b, f)) {
            score += sign * passed_pawn_bonus(f);
            Cell enemy_king = (f.color == '+') ? opp_king : my_king;
            if (enemy_king.valid()) score += sign * square_rule_bonus(f, enemy_king);
        }
    }

    return score;
}

// общая оценка: сама выбирает режим
int evaluate_position(const InputData& in) {
    BoardMap b = make_board_map(in);
    return is_endgame_position(in) ? evaluate_endgame(in, b) : evaluate_midgame(in, b);
}

class ChessBot {
private:
    struct RootCandidate {
        Move move;
        int order = 0;
        bool immediate_back = false;
        int repeat_freq = 0;
        bool progress = false;
        bool non_repeat = false;
        bool queen_suicide = false;
        bool gives_check = false;
    };

    OpeningBook opening;
    bool bot_is_white = true;
    Move last_move;
    std::unordered_map<std::string, int> recent_move_counts;
    std::deque<std::string> recent_moves;
    std::deque<std::string> recent_opponent_moves;
    int no_progress_streak = 0;

    using Clock = std::chrono::steady_clock;
    Clock::time_point deadline;
    bool search_timeout = false;

    bool time_is_over() const {
        return Clock::now() >= deadline;
    }

    static void remember_move_str(std::deque<std::string>& q, std::unordered_map<std::string, int>& cnt, const std::string& s) {
        q.push_back(s);
        cnt[s]++;
        const std::size_t keep = 20;
        if (q.size() > keep) {
            std::string old = q.front();
            q.pop_front();
            auto it = cnt.find(old);
            if (it != cnt.end()) {
                it->second--;
                if (it->second <= 0) cnt.erase(it);
            }
        }
    }

    void remember_our_move(const Move& m) {
        remember_move_str(recent_moves, recent_move_counts, m.str());
    }

    void remember_opponent_move(const std::string& move) {
        recent_opponent_moves.push_back(move);
        const std::size_t keep = 20;
        if (recent_opponent_moves.size() > keep) recent_opponent_moves.pop_front();
    }

    static bool has_two_move_cycle(const std::deque<std::string>& q) {
        if (q.size() < 4) return false;
        std::size_t n = q.size();
        return q[n - 1] == q[n - 3] && q[n - 2] == q[n - 4];
    }

    bool we_are_cycling() const {
        return has_two_move_cycle(recent_moves);
    }

    bool opponent_is_cycling() const {
        if (has_two_move_cycle(recent_opponent_moves)) return true;
        for (std::size_t i = 0; i < recent_opponent_moves.size(); i++) {
            int freq = 0;
            for (std::size_t j = 0; j < recent_opponent_moves.size(); j++) {
                if (recent_opponent_moves[i] == recent_opponent_moves[j]) freq++;
            }
            if (freq >= 3) return true;
        }
        return false;
    }

    bool is_endgame_mode(const InputData& in) const {
        return is_endgame_position(in) || we_are_cycling() || opponent_is_cycling();
    }

    int eval_for_side(const InputData& in, char side) const {
        int e = evaluate_position(in);
        return (side == '+') ? e : -e;
    }

    int capture_order_score(const BoardMap& b, const Move& m, char side) const {
        const PieceEntry* mover = find_piece_entry(b, m.from.x, m.from.y);
        const PieceEntry* target = find_piece_entry(b, m.to.x, m.to.y);
        int cap = target ? piece_weight(target->second) : 1;
        int att = (mover && mover->first == side) ? piece_weight(mover->second) : 1;
        return cap * 100 - att;
    }

    int quiescence(const InputData& in, int alpha, int beta, char side, int depth_left) {
        if (time_is_over()) {
            search_timeout = true;
            return 0;
        }

        int stand_pat = eval_for_side(in, side);
        if (stand_pat >= beta) return beta;
        if (stand_pat > alpha) alpha = stand_pat;
        if (depth_left <= 0) return alpha;

        BoardMap b = make_board_map(in);
        auto moves = generate_all_legal_moves_color(in, side);
        std::vector<std::pair<int, Move>> captures;
        captures.reserve(moves.size());
        for (const auto& m : moves) {
            if (!is_capture_move_on_board(b, m, side)) continue;
            captures.push_back({capture_order_score(b, m, side), m});
        }
        if (captures.empty()) return alpha;

        std::stable_sort(captures.begin(), captures.end(), [](const auto& a, const auto& b2) {
            return a.first > b2.first;
        });

        for (const auto& sm : captures) {
            InputData next = make_next_state(in, b, sm.second);
            int score = -quiescence(next, -beta, -alpha, (side == '+') ? '-' : '+', depth_left - 1);
            if (search_timeout) return 0;
            if (score >= beta) return beta;
            if (score > alpha) alpha = score;
        }
        return alpha;
    }

    int alphabeta(const InputData& in, int depth, int alpha, int beta, char side) {
        static const int MATE_SCORE = 100000000;
        if (time_is_over()) {
            search_timeout = true;
            return 0;
        }
        if (depth == 0) return quiescence(in, alpha, beta, side, 3);

        auto moves = generate_all_legal_moves_color(in, side);
        if (moves.empty()) {
            BoardMap b = make_board_map(in);
            if (is_in_check_on_board(b, side)) {
                return -MATE_SCORE + depth;
            }
            return 0;
        }

        for (const auto& m : moves) {
            InputData next = make_next_state(in, m);
            int score = -alphabeta(next, depth - 1, -beta, -alpha, (side == '+') ? '-' : '+');
            if (search_timeout) return 0;
            if (score > alpha) alpha = score;
            if (alpha >= beta) break;
        }
        return alpha;
    }

    Move get_opening_move(const InputData& in, int variation_seed) const {
        OpeningBook::BookMove bm = opening.lookup_by_hash(bot_is_white, in, variation_seed);
        if (bm.move.size() != 4) return Move();
        Move m;
        m.from = {bm.move[0], bm.move[1]};
        m.to = {bm.move[2], bm.move[3]};
        auto legal = generate_all_legal_moves_color(in, '+');
        for (const auto& lm : legal) {
            if (lm.from == m.from && lm.to == m.to) return m;
        }
        return Move();
    }

    Move get_search_move(const InputData& in, bool endgame_mode) {
        BoardMap board = make_board_map(in);
        auto root_moves = generate_all_legal_moves_color(in, '+');
        if (root_moves.empty()) return Move();

        bool opening_bias = !endgame_mode && is_opening_phase(in);
        bool hard_loop_mode = no_progress_streak >= 3 || we_are_cycling() || opponent_is_cycling();

        std::vector<RootCandidate> candidates;
        candidates.reserve(root_moves.size());
        for (const auto& m : root_moves) {
            RootCandidate c;
            c.move = m;
            c.order = score_move_ordering(board, m, opening_bias);
            c.immediate_back = (m.from == last_move.to && m.to == last_move.from);
            auto it = recent_move_counts.find(m.str());
            c.repeat_freq = (it == recent_move_counts.end()) ? 0 : it->second;
            c.progress = is_progress_move_on_board(board, m, '+');
            c.non_repeat = !c.immediate_back && c.repeat_freq == 0;
            BoardMap nb = apply_move_copy(board, m);
            c.gives_check = is_in_check_on_board(nb, '-');
            if (c.gives_check) c.order += 220;
            const PieceEntry* mover = find_piece_entry(board, m.from.x, m.from.y);
            const PieceEntry* captured = find_piece_entry(board, m.to.x, m.to.y);
            int captured_value = captured ? piece_weight(captured->second) : 0;
            if (mover && mover->second == FigureType::QUEEN) {
                bool attacked = is_square_attacked(nb, m.to.x, m.to.y, '-');
                if (attacked && captured_value < piece_weight(FigureType::QUEEN)) {
                    c.queen_suicide = true;
                    c.order -= 2500 + (piece_weight(FigureType::QUEEN) - captured_value) * 120;
                }
            }
            candidates.push_back(c);
        }

        if (endgame_mode) {
            std::vector<RootCandidate> non_back_pool;
            non_back_pool.reserve(candidates.size());
            for (const auto& c : candidates) {
                if (!c.immediate_back) non_back_pool.push_back(c);
            }
            if (!non_back_pool.empty()) candidates.swap(non_back_pool);
        }

        if (hard_loop_mode) {
            std::vector<RootCandidate> progress_pool;
            progress_pool.reserve(candidates.size());
            for (const auto& c : candidates) {
                if ((c.progress || c.gives_check) && c.non_repeat) progress_pool.push_back(c);
            }
            if (!progress_pool.empty()) candidates.swap(progress_pool);
        }

        std::stable_sort(candidates.begin(), candidates.end(), [](const RootCandidate& a, const RootCandidate& b) {
            return a.order > b.order;
        });

        int budget_ms = endgame_mode ? 950 : 650;
        int max_depth = endgame_mode ? 20 : 16;
        deadline = Clock::now() + std::chrono::milliseconds(budget_ms);
        search_timeout = false;

        const int INF = 1000000000;
        Move best = candidates.front().move;
        int best_score = -INF;

        for (int depth = 1; depth <= max_depth; depth++) {
            if (time_is_over()) break;
            int local_best_score = -INF;
            Move local_best = best;

            int local_non_repeat_score = -INF;
            Move local_non_repeat;
            bool has_non_repeat = false;

            for (const auto& c : candidates) {
                if (time_is_over()) {
                    search_timeout = true;
                    break;
                }

                InputData next = make_next_state(in, board, c.move);
                int score = -alphabeta(next, depth - 1, -INF, INF, '-');

                if (c.immediate_back) score -= (endgame_mode ? 3000 : 600);
                int repeat_penalty = (endgame_mode ? 260 : 160) + no_progress_streak * 35;
                score -= c.repeat_freq * repeat_penalty;

                if (!c.progress) {
                    score -= (endgame_mode ? 70 : 25);
                    if (no_progress_streak >= 2) score -= no_progress_streak * 25;
                }
                if (c.gives_check) score += 180;
                if (hard_loop_mode && !c.non_repeat) score -= 220;
                if (hard_loop_mode && !c.progress && !c.gives_check) score -= 180;
                if (c.queen_suicide) score -= 3200;

                if (search_timeout) break;

                if (score > local_best_score) {
                    local_best_score = score;
                    local_best = c.move;
                }
                if (c.non_repeat && score > local_non_repeat_score) {
                    local_non_repeat_score = score;
                    local_non_repeat = c.move;
                    has_non_repeat = true;
                }
            }

            if (search_timeout) break;
            if (hard_loop_mode && has_non_repeat) {
                best = local_non_repeat;
                best_score = local_non_repeat_score;
            } else {
                best = local_best;
                best_score = local_best_score;
            }
        }

        (void)best_score;
        return best;
    }

public:
    ChessBot() = default;

    void set_side(bool is_white) {
        bot_is_white = is_white;
        G_BOT_IS_WHITE = is_white;
    }

    void add_opponent_move(const std::string& opp_move) {
        remember_opponent_move(opp_move);
    }

    Move get_move(const InputData& in) {
        bool loop_now = we_are_cycling() || opponent_is_cycling();
        Move best;
        if (!loop_now) best = get_opening_move(in, in.pong);
        if (!best.valid()) best = get_search_move(in, is_endgame_mode(in));

        auto legal = generate_all_legal_moves_color(in, '+');
        bool best_is_legal = false;
        for (const auto& m : legal) {
            if (m.from == best.from && m.to == best.to) {
                best_is_legal = true;
                break;
            }
        }
        if (!best_is_legal) {
            BoardMap in_board = make_board_map(in);
            int dir = pawn_dir_for_color(in_board, '+');
            char promo_y = (dir > 0) ? '8' : '1';
            best = Move();
            for (const auto& m : legal) {
                const PieceEntry* mover = find_piece_entry(in_board, m.from.x, m.from.y);
                if (!mover || mover->second != FigureType::PAWN) { best = m; break; }
                if (m.to.y != promo_y) { best = m; break; }
            }
            if (!best.valid() && !legal.empty()) best = legal.front();
        }

        if (best.valid()) {
            BoardMap b = make_board_map(in);
            if (is_progress_move_on_board(b, best, '+')) no_progress_streak = 0;
            else no_progress_streak++;
            last_move = best;
            remember_our_move(best);
        }

        return best;
    }
};

int main() {
    ChessBot bot;
    InputData in;
    InputData prev;
    bool has_prev = false;
    bool side_known = false;

    while (true) {
        if (!read_input(in)) break;

        if (!side_known && in.count > 0) {
            bool is_white = detect_side(in);
            bot.set_side(is_white);
            side_known = true;
        }

        if (has_prev) {
            std::string opp = infer_opponent_move(prev, in);
            if (!opp.empty()) bot.add_opponent_move(opp);
        }

        Move best = bot.get_move(in);
        if (best.valid()) {
            int next_ping = (in.pong <= 0) ? 1 : (in.pong + 1);
            output_move(best.from, best.to, next_ping);
        }

        prev = in;
        has_prev = true;
    }

    return 0;
}
