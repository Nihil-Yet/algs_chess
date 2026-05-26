#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <unordered_map>
#include <map>
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
    data.ep_square = {ep[0], ep[1]};

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

// дебагер для нас
void debug(const std::string& msg) {
    std::cout << "// " << msg << '\n';
    std::cout.flush();
}

// дебаг пешек: считаем и печатаем расстановку
std::string pawn_layout_debug(const InputData& in) {
    std::vector<std::string> plus_pawns;
    std::vector<std::string> minus_pawns;

    for (const auto& f : in.figures) {
        if (f.type != FigureType::PAWN) continue;
        std::string sq = f.pos.str();
        if (f.color == '+') plus_pawns.push_back(sq);
        else minus_pawns.push_back(sq);
    }

    std::sort(plus_pawns.begin(), plus_pawns.end());
    std::sort(minus_pawns.begin(), minus_pawns.end());

    std::string out = "Pawns: += " + std::to_string(plus_pawns.size()) + " [";
    for (std::size_t i = 0; i < plus_pawns.size(); i++) {
        if (i) out += " ";
        out += plus_pawns[i];
    }
    out += "], -= " + std::to_string(minus_pawns.size()) + " [";
    for (std::size_t i = 0; i < minus_pawns.size(); i++) {
        if (i) out += " ";
        out += minus_pawns[i];
    }
    out += "]";
    return out;
}

// отправляем ход серверу: клеткачисло, пробел и пинг с переходом на новую строку
void output_move(const Cell& from, const Cell& to, int ping) {
    std::cout << from.x << from.y << to.x << to.y << ' ' << ping << '\n';
    std::cout.flush();
}

// определяем сторону только по первому входному списку фигур:
// сначала пробуем по пешкам, потом fallback по королю
bool detect_side(const InputData& in) {
    int plus_pawn_sum = 0, plus_pawn_cnt = 0;
    int minus_pawn_sum = 0, minus_pawn_cnt = 0;

    for (const auto& f : in.figures) {
        if (f.type != FigureType::PAWN) continue;
        int ry = f.pos.y - '0';
        if (f.color == '+') { plus_pawn_sum += ry; plus_pawn_cnt++; }
        else { minus_pawn_sum += ry; minus_pawn_cnt++; }
    }

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
    for (auto d : std::vector<std::pair<int, int>>{{1, 1}, {1, -1}, {-1, 1}, {-1, -1}}) {
        char x = tx, y = ty;
        while (true) {
            x = static_cast<char>(x + d.first);
            y = static_cast<char>(y + d.second);
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
    for (auto d : std::vector<std::pair<int, int>>{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
        char x = tx, y = ty;
        while (true) {
            x = static_cast<char>(x + d.first);
            y = static_cast<char>(y + d.second);
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
void gen_slider_moves(const BoardMap& b, std::vector<Move>& out, const Cell& from, char my_color, const std::vector<std::pair<int, int>>& dirs) {
    for (const auto& d : dirs) {
        char x = from.x;
        char y = from.y;
        while (true) {
            x = static_cast<char>(x + d.first);
            y = static_cast<char>(y + d.second);
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
    char start_y = (my_color == '+') ? '2' : '7';
    char enemy_color = (my_color == '+') ? '-' : '+';

    // шаг на 1
    if (on_board(from.x, one_y) && !has_piece(b, from.x, one_y)) {
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
std::vector<Move> generate_piece_moves(const Figure& f, const BoardMap& b, const InputData& in) {
    std::vector<Move> out;

    if (f.type == FigureType::PAWN) {
        gen_pawn_moves(b, out, f.pos, f.color, in.ep_square);
    } else if (f.type == FigureType::KNIGHT) {
        gen_knight_moves(b, out, f.pos, f.color);
    } else if (f.type == FigureType::BISHOP) {
        gen_slider_moves(b, out, f.pos, f.color, {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}});
    } else if (f.type == FigureType::ROOK) {
        gen_slider_moves(b, out, f.pos, f.color, {{1, 0}, {-1, 0}, {0, 1}, {0, -1}});
    } else if (f.type == FigureType::QUEEN) {
        gen_slider_moves(b, out, f.pos, f.color, {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}});
    } else if (f.type == FigureType::KING) {
        bool can_long = (f.color == '+') ? in.castling[0] : in.castling[2];
        bool can_short = (f.color == '+') ? in.castling[1] : in.castling[3];
        gen_king_moves(b, out, f.pos, f.color, can_long, can_short);
    }
    return out;
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
std::vector<Move> generate_all_moves_color(const InputData& in, char color) {
    std::vector<Move> all;
    BoardMap b = make_board_map(in);
    for (const auto& f : in.figures) {
        if (f.color != color) continue;
        auto one = generate_piece_moves(f, b, in);
        all.insert(all.end(), one.begin(), one.end());
    }
    return all;
}

// фильтр легальности для любого цвета
std::vector<Move> filter_legal_moves_color(const InputData& in, const std::vector<Move>& pseudo, char color) {
    std::vector<Move> legal;
    BoardMap b = make_board_map(in);
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
    auto pseudo = generate_all_moves_color(in, color);
    return filter_legal_moves_color(in, pseudo, color);
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

// фигура на клетке (если есть)
const Figure* find_figure_at(const InputData& in, char x, char y) {
    for (const auto& f : in.figures) {
        if (f.pos.x == x && f.pos.y == y) return &f;
    }
    return nullptr;
}

// простенькая оценка хода для мидгейма ([CHECK] потом заменим на норм поиск)
int score_move_simple(const InputData& in, const Move& m) {
    int score = 0;

    // если бьем фигуру, то плюс к оценке
    const Figure* captured = find_figure_at(in, m.to.x, m.to.y);
    if (captured && captured->color == '-') {
        score += piece_weight(captured->type) * 100;
    }

    // центр (d4/e4/d5/e5)
    if ((m.to.x == 'd' || m.to.x == 'e') && (m.to.y == '4' || m.to.y == '5')) {
        score += 10;
    }

    // маленький бонус за продвижение пешки
    const Figure* mover = find_figure_at(in, m.from.x, m.from.y);
    if (mover && mover->type == FigureType::PAWN) {
        score += 2;
    }

    // если встали на битую клетку - штраф
    BoardMap b = make_board_map(in);
    BoardMap nb = apply_move_copy(b, m);
    if (is_square_attacked(nb, m.to.x, m.to.y, '-')) {
        int lose = mover ? piece_weight(mover->type) : 1;
        score -= lose * 50;
    }

    return score;
}

// легкая версия оценки для сортировки (без тяжелой проверки "битая клетка")
int score_move_ordering(const InputData& in, const Move& m) {
    int score = 0;
    const Figure* captured = find_figure_at(in, m.to.x, m.to.y);
    if (captured && captured->color == '-') score += piece_weight(captured->type) * 100;
    if ((m.to.x == 'd' || m.to.x == 'e') && (m.to.y == '4' || m.to.y == '5')) score += 10;
    const Figure* mover = find_figure_at(in, m.from.x, m.from.y);
    if (mover && mover->type == FigureType::PAWN) score += 2;
    return score;
}

// пешка на 1/8 не должна идти [CHECK!!!]
bool is_pawn_promotion_move(const InputData& in, const Move& m) {
    const Figure* mover = find_figure_at(in, m.from.x, m.from.y);
    if (!mover || mover->type != FigureType::PAWN) return false;
    return (m.to.y == '1' || m.to.y == '8');
}

// убираем ходы с пешкой на 1/8 (promotion пока не поддержан)
std::vector<Move> without_promotion_moves(const InputData& in, const std::vector<Move>& moves) {
    std::vector<Move> out;
    out.reserve(moves.size());
    for (const auto& m : moves) {
        if (!is_pawn_promotion_move(in, m)) out.push_back(m);
    }
    return out;
}

// делаем следующее состояние после хода (с упрощениями для мидгейма)
InputData make_next_state(const InputData& in, const Move& m) {
    InputData out = in;
    BoardMap b = make_board_map(in);
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
    int advance = (p.color == '+') ? (rank - 2) : (7 - rank);
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
    int steps = (p.color == '+') ? ('8' - p.pos.y) : (p.pos.y - '1');
    if (steps <= 0) return 0;
    Cell promo = {p.pos.x, (p.color == '+') ? '8' : '1'};
    int king_dist = manhattan(enemy_king, promo);
    if (king_dist > steps) return 80; // король не в квадрате пешки
    return 0;
}

// обычная оценка (мидгейм)
int evaluate_midgame(const InputData& in) {
    int score = 0;
    for (const auto& f : in.figures) {
        int w = piece_weight(f.type) * 100;
        score += (f.color == '+') ? w : -w;
    }

    // безопасность королей
    BoardMap b = make_board_map(in);
    Cell my_king = find_king_cell(b, '+');
    Cell opp_king = find_king_cell(b, '-');
    if (my_king.valid() && is_square_attacked(b, my_king.x, my_king.y, '-')) score -= 120;
    if (opp_king.valid() && is_square_attacked(b, opp_king.x, opp_king.y, '+')) score += 120;

    return score;
}

// отдельная оценка эндшпиля
int evaluate_endgame(const InputData& in) {
    int score = 0;
    BoardMap b = make_board_map(in);
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
    return is_endgame_position(in) ? evaluate_endgame(in) : evaluate_midgame(in);
}

class ChessBot {
private:
    OpeningBook opening;            // библиотека дебютов
    std::string move_history;       // история ходов через '_'
    bool bot_is_white = true;       // сторона изначально считается белой
    Move last_move;                 // последний наш ход (для анти-цикла)

    // добавляем полуход в историю
    void append_history(const std::string& ply) {
        move_history += (move_history.empty() ? "" : "_") + ply;
    }

    // текущий ключ позиции
    std::string make_position_key() const {
        return move_history;
    }

    bool has_piece_at(const InputData& in, char x, char y, char color) const {
        for (const auto& f : in.figures) {
            if (f.pos.x == x && f.pos.y == y && f.color == color) return true;
        }
        return false;
    }

    bool has_piece_at(const InputData& in, char x, char y, char color, FigureType type) const {
        for (const auto& f : in.figures) {
            if (f.pos.x == x && f.pos.y == y && f.color == color && f.type == type) return true;
        }
        return false;
    }

    // старт белых: только реальная стартовая позиция, а не "кто-то стоит на e2"
    bool is_white_start_position(const InputData& in) const {
        if (in.count != 32) return false;
        return has_piece_at(in, 'e', '2', '+', FigureType::PAWN) &&
               has_piece_at(in, 'e', '1', '+', FigureType::KING) &&
               has_piece_at(in, 'e', '7', '-', FigureType::PAWN) &&
               has_piece_at(in, 'e', '8', '-', FigureType::KING);
    }

    using Clock = std::chrono::steady_clock;
    Clock::time_point deadline;
    bool search_timeout = false;

    bool time_is_over() const {
        return Clock::now() >= deadline;
    }

    // alpha-beta negamax (по времени)
    int alphabeta(const InputData& in, int depth, int alpha, int beta, char side) {
        if (time_is_over()) {
            search_timeout = true;
            return 0;
        }
        if (depth == 0) {
            int e = evaluate_position(in);
            return (side == '+') ? e : -e;
        }

        auto legal = generate_all_legal_moves_color(in, side);
        auto moves = without_promotion_moves(in, legal);
        if (moves.empty()) {
            int e = evaluate_position(in);
            return (side == '+') ? e : -e;
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

    // совершаем ход из библиотеки дебютов: откуда -> кудась
    Move get_opening_move(int variation_seed, const InputData& in) {
        std::string key = make_position_key();
        OpeningBook::BookMove response =
            key.empty() ? OpeningBook::BookMove{"", ""} : opening.lookup(bot_is_white, key, variation_seed);

        // если истории еще нет, но мы белые и это реально старт - берем первый ход белых
        if (response.move.empty() && key.empty() && bot_is_white && is_white_start_position(in)) {
            // старт белых: первый ход из пустого ключа
            response = opening.lookup(true, "", variation_seed);
        }

        // fallback по хэшу позиции (на случай перезапуска и пустой истории)
        if (response.move.empty()) {
            response = opening.lookup_by_hash(bot_is_white, in, variation_seed);
        }


        const std::string& move = response.move;
        const std::string& opening_name = response.opening;
        if (!move.empty() && move.length() == 4) {
            Move m;
            m.from = {move[0], move[1]};
            m.to = {move[2], move[3]};
            // [CHECK] - ДЕБАГ ДЕБЮТОВ, ШОБ ЗНАТЬ КАКОЙ СЕЙЧАС РАБОТАЕТ. ПОТОМ УДАЛИТЬ
            debug("Opening(book): depth=book, дебют=" + opening_name + ", ход=" + move);
            return m;
        }
        return Move();
    }

    // если библиотеки не нашли ход - считаем через alpha-beta (мидгейм/эндшпиль)
    Move get_search_move(const InputData& in) {
        bool endgame = is_endgame_position(in);
        auto root_legal = generate_all_legal_moves_color(in, '+');
        auto moves = without_promotion_moves(in, root_legal);
        if (moves.empty()) return Move();

        // быстрый ordering: считаем один раз, потом сортируем
        std::vector<std::pair<int, Move>> scored;
        scored.reserve(moves.size());
        for (const auto& m : moves) scored.push_back({score_move_ordering(in, m), m});
        std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
            return a.first > b.first;
        });
        for (std::size_t i = 0; i < moves.size(); i++) moves[i] = scored[i].second;

        int budget_ms = endgame ? 500 : 350; // держим запас до серверного таймаута
        deadline = Clock::now() + std::chrono::milliseconds(budget_ms);
        search_timeout = false;

        Move best = moves[0];       // fallback, если время резко кончится
        int best_score = -1000000000;
        int completed_depth = 0;

        // iterative deepening: 1,2,3... пока есть время
        for (int depth = 1; depth <= 12; depth++) {
            if (time_is_over()) break;
            int local_best_score = -1000000000;
            Move local_best = best;

            for (const auto& m : moves) {
                if (time_is_over()) { search_timeout = true; break; }
                InputData next = make_next_state(in, m);
                int s = -alphabeta(next, depth - 1, -1000000000, 1000000000, '-');
                // анти-зацикливание: не любим ходы "туда-сюда" той же фигурой
                if (m.from == last_move.to && m.to == last_move.from) s -= 40;
                if (search_timeout) break;
                if (s > local_best_score) {
                    local_best_score = s;
                    local_best = m;
                }
            }

            if (search_timeout) break;
            best = local_best;
            best_score = local_best_score;
            completed_depth = depth;
        }

        debug(std::string(endgame ? "Endgame(ab): ходов=" : "Midgame(ab): ходов=") + std::to_string(moves.size()) +
              ", depth=" + std::to_string(completed_depth) +
              ", score=" + std::to_string(best_score));
        return best;
    }

public:
    // конструктор по умолчанию
    ChessBot() = default;
    // устанавливаем сторону
    void set_side(bool is_white) {
        bot_is_white = is_white;
        G_BOT_IS_WHITE = is_white;
    }
    // совершает ход 
    Move get_move(const InputData& in) {
        Move m = get_opening_move(in.pong, in);
        if (!m.valid()) {
            m = get_search_move(in);
        }
        // добавляем в историю, если ход валиден
        if (m.valid()) {
            append_history(m.str());
            last_move = m;
        }
        return m;
    }
    // добавляем ход соперника
    void add_opponent_move(const std::string& opp_move) {
        append_history(opp_move);
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
        if (!read_input(in)) break;

        // определяем сторону бота один раз
        if (!side_known && in.count > 0) {
            bool is_white = detect_side(in);
            bot.set_side(is_white);
            side_known = true;
            debug(std::string("Сторона бота: ") + (is_white ? "white" : "black"));
        }

        // дебаг пешек каждый ход (пушо полезно для эндшпиля/анализа)
        debug(pawn_layout_debug(in));

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
            debug("Будет реализовано в midgame");
        }

        prev = in;          // сохраняем текущую, как предыдущую для следующего
        has_prev = true;    // есть предыдущий
    }

    return 0;
}
