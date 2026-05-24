#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <climits>

// ============================================================================
// === КОНСТАНТЫ И ТИПЫ ===
// ============================================================================

enum class FigureType {
    PAWN = 1, KNIGHT = 2, BISHOP = 3, ROOK = 4, QUEEN = 5, KING = 6
};

struct Cell {
    char x = '\0', y = '\0';
    bool valid() const { return x >= 'a' && x <= 'h' && y >= '1' && y <= '8'; }
    std::string str() const { return valid() ? std::string() + x + y : ""; }
    bool operator==(const Cell& o) const { return x == o.x && y == o.y; }
};

struct Figure {
    Cell pos;
    char color;  // '+' или '-'
    FigureType type;
};

// ============================================================================
// === ВВОД/ВЫВОД ===
// ============================================================================

struct InputData {
    int pong = 0;
    std::vector<Figure> figures;
    Cell ep_square;
    bool castling[4] = {false, false, false, false}; // [WK, WQ, BK, BQ]
};

void read_input(InputData& data) {
    std::string line;
    
    if (!(std::cin >> data.pong)) return;
    int n; std::cin >> n;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    
    data.figures.resize(n);
    for (int i = 0; i < n; i++) {
        std::getline(std::cin, line);
        if (line.size() < 6) continue;
        data.figures[i].pos.x = line[0];
        data.figures[i].pos.y = line[1];
        data.figures[i].color = line[3];
        data.figures[i].type  = static_cast<FigureType>(line[5] - '0');
    }
    
    // ep_square
    std::getline(std::cin, line);
    if (line.size() >= 2 && line[0] >= 'a' && line[0] <= 'h' && line[1] >= '1' && line[1] <= '8') {
        data.ep_square = {line[0], line[1]};
    } else {
        data.ep_square = {'\0', '\0'};
    }
    
    // castling: "a h a h" или "- - a h"
    std::getline(std::cin, line);
    std::istringstream iss(line);
    char ws, wl, bs, bl;
    if (iss >> ws >> wl >> bs >> bl) {
        data.castling[0] = (ws != '-'); // white short (kingside)
        data.castling[1] = (wl != '-'); // white long  (queenside)
        data.castling[2] = (bs != '-'); // black short
        data.castling[3] = (bl != '-'); // black long
    }
}

void debug(const std::string& msg) {
    std::cout << "# " << msg << '\n';
    std::cout.flush();
}

void output_move(const Cell& from, const Cell& to, int ping) {
    std::cout << from.x << from.y << to.x << to.y << ' ' << ping << '\n';
    std::cout.flush();
}

// ============================================================================
// === ДОСКА И ХОДЫ ===
// ============================================================================

// Код фигуры: знак = цвет, модуль = тип. +1 = белая пешка, -6 = чёрный король
inline int encode(char color, FigureType t) { return (color == '+' ? 1 : -1) * static_cast<int>(t); }
inline char decode_color(int code) { return code > 0 ? '+' : '-'; }
inline FigureType decode_type(int code) { return static_cast<FigureType>(std::abs(code)); }

struct Board {
    int cells[8][8] = {}; // [file][rank], 0 = пусто
    bool white_to_move = true;
    Cell ep = {'\0','\0'};
    bool castle[4] = {false,false,false,false};
    
    static int f(char x) { return x - 'a'; }
    static int r(char y) { return y - '1'; }
    static char xf(int i) { return 'a' + i; }
    static char yf(int i) { return '1' + i; }
    
    bool in(int file, int rank) const { return file>=0 && file<8 && rank>=0 && rank<8; }
    int& at(char x, char y) { return cells[f(x)][r(y)]; }
    int at(char x, char y) const { return cells[f(x)][r(y)]; }
    
    void clear() {
        for(int i=0;i<8;i++) for(int j=0;j<8;j++) cells[i][j]=0;
        white_to_move = true;
        ep = {'\0','\0'};
        for(int i=0;i<4;i++) castle[i]=false;
    }
    
    void load(const InputData& in, bool bot_is_white) {
        clear();
        white_to_move = bot_is_white;
        ep = in.ep_square;
        for(int i=0;i<4;i++) castle[i] = in.castling[i];
        for(const auto& fig : in.figures) {
            cells[f(fig.pos.x)][r(fig.pos.y)] = encode(fig.color, fig.type);
        }
    }
};

struct Move {
    Cell from, to;
    FigureType promo = FigureType::QUEEN;
    bool is_castle = false, is_ep = false;
    
    std::string str() const { return from.str() + to.str(); }
    bool valid() const { return from.valid() && to.valid(); }
};

// ============================================================================
// === ГЕНЕРАЦИЯ ХОДОВ ===
// ============================================================================

class MoveGen {
public:
    static std::vector<Move> generate(const Board& b, bool for_white) {
        std::vector<Move> moves;
        for (int f = 0; f < 8; f++) for (int r = 0; r < 8; r++) {
            int code = b.cells[f][r];
            if (code == 0) continue;
            char color = decode_color(code);
            if ((color == '+') != for_white) continue;
            FigureType t = decode_type(code);
            Cell from{Board::xf(f), Board::yf(r)};
            
            switch (t) {
                case FigureType::PAWN:   gen_pawn(b, from, color, moves); break;
                case FigureType::KNIGHT: gen_sliding(b, from, color, moves, knight_dirs); break;
                case FigureType::BISHOP: gen_sliding(b, from, color, moves, bishop_dirs); break;
                case FigureType::ROOK:   gen_sliding(b, from, color, moves, rook_dirs); break;
                case FigureType::QUEEN:  gen_sliding(b, from, color, moves, queen_dirs); break;
                case FigureType::KING:   gen_king(b, from, color, moves); break;
            }
        }
        if (for_white) gen_castling(b, true, moves);
        else gen_castling(b, false, moves);
        return moves;
    }
    
private:
    // Направления: {df, dr}
    static constexpr int knight_dirs[8][2] = {{1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}};
    static constexpr int bishop_dirs[4][2] = {{1,1},{1,-1},{-1,1},{-1,-1}};
    static constexpr int rook_dirs[4][2]   = {{1,0},{-1,0},{0,1},{0,-1}};
    static constexpr int queen_dirs[8][2]  = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    static constexpr int king_dirs[8][2]   = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    
    static void gen_pawn(const Board& b, Cell from, char color, std::vector<Move>& out) {
        int dir = (color == '+') ? 1 : -1;
        int start_rank = (color == '+') ? 1 : 6;
        int promo_rank = (color == '+') ? 7 : 0;
        int f = Board::f(from.x), r = Board::r(from.y);
        
        // Тихий ход вперёд
        int nr = r + dir;
        if (b.in(f, nr) && b.cells[f][nr] == 0) {
            if (nr == promo_rank) {
                for (auto promo : {FigureType::QUEEN, FigureType::ROOK, FigureType::BISHOP, FigureType::KNIGHT}) {
                    out.push_back({from, {from.x, Board::yf(nr)}, promo});
                }
            } else {
                out.push_back({from, {from.x, Board::yf(nr)}});
                // Двойной шаг
                if (r == start_rank) {
                    int nr2 = r + 2*dir;
                    if (b.in(f, nr2) && b.cells[f][nr2] == 0) {
                        out.push_back({from, {from.x, Board::yf(nr2)}});
                    }
                }
            }
        }
        // Взятия
        for (int df : {-1, 1}) {
            int nf = f + df, nr = r + dir;
            if (!b.in(nf, nr)) continue;
            int target = b.cells[nf][nr];
            if (target != 0 && decode_color(target) != color) {
                if (nr == promo_rank) {
                    for (auto promo : {FigureType::QUEEN, FigureType::ROOK, FigureType::BISHOP, FigureType::KNIGHT}) {
                        out.push_back({from, {Board::xf(nf), Board::yf(nr)}, promo});
                    }
                } else {
                    out.push_back({from, {Board::xf(nf), Board::yf(nr)}});
                }
            }
            // Взятие на проходе
            if (b.ep.valid() && Board::f(b.ep.x) == nf && Board::r(b.ep.y) == nr) {
                Move m{from, b.ep}; m.is_ep = true;
                out.push_back(m);
            }
        }
    }
    
    static void gen_sliding(const Board& b, Cell from, char color, std::vector<Move>& out, const int dirs[][2], int count = 8) {
        int f = Board::f(from.x), r = Board::r(from.y);
        for (int d = 0; d < count; d++) {
            int df = dirs[d][0], dr = dirs[d][1];
            for (int step = 1; ; step++) {
                int nf = f + step*df, nr = r + step*dr;
                if (!b.in(nf, nr)) break;
                int target = b.cells[nf][nr];
                if (target == 0) {
                    out.push_back({from, {Board::xf(nf), Board::yf(nr)}});
                } else {
                    if (decode_color(target) != color) {
                        out.push_back({from, {Board::xf(nf), Board::yf(nr)}});
                    }
                    break;
                }
            }
        }
    }
    
    static void gen_king(const Board& b, Cell from, char color, std::vector<Move>& out) {
        gen_sliding(b, from, color, out, king_dirs, 8);
    }
    
    static void gen_castling(const Board& b, bool white, std::vector<Move>& out) {
        // Упрощённо: проверяем только права рокировки и пустоту полей
        int rank = white ? 0 : 7;
        int base = white ? 0 : 3; // индексы в castle[]
        
        // Kingside: e1-g1 или e8-g8
        if (b.castle[base] && b.cells[5][rank]==0 && b.cells[6][rank]==0) {
            if (b.cells[7][rank] != 0 && decode_type(b.cells[7][rank]) == FigureType::ROOK) {
                Move m{{'e', Board::yf(rank)}, {'g', Board::yf(rank)}};
                m.is_castle = true; out.push_back(m);
            }
        }
        // Queenside: e1-c1 или e8-c8
        if (b.castle[base+1] && b.cells[1][rank]==0 && b.cells[2][rank]==0 && b.cells[3][rank]==0) {
            if (b.cells[0][rank] != 0 && decode_type(b.cells[0][rank]) == FigureType::ROOK) {
                Move m{{'e', Board::yf(rank)}, {'c', Board::yf(rank)}};
                m.is_castle = true; out.push_back(m);
            }
        }
    }
};

// ============================================================================
// === ОЦЕНКА ПОЗИЦИИ ===
// ============================================================================

class Evaluator {
public:
    static const int VAL[7]; // 0 unused, 1..6 = pawn..king
    
    static int material(const Board& b, bool for_white) {
        int sum = 0;
        for(int f=0;f<8;f++) for(int r=0;r<8;r++) {
            int c = b.cells[f][r];
            if (c == 0) continue;
            int val = VAL[static_cast<int>(decode_type(c))];
            if (decode_color(c) == '+' == for_white) sum += val;
            else sum -= val;
        }
        return sum;
    }
    
    static int mobility(const Board& b, bool for_white) {
        return static_cast<int>(MoveGen::generate(b, for_white).size()) * 10;
    }
    
    static int center_control(const Board& b) {
        int score = 0;
        for (auto [f,r] : {std::make_pair(3,3), std::make_pair(4,3), std::make_pair(3,4), std::make_pair(4,4)}) {
            int c = b.cells[f][r];
            if (c == 0) continue;
            score += (decode_color(c) == '+') ? 5 : -5;
        }
        return score;
    }
    
    static int king_safety(const Board& b, bool for_white) {
        // Упрощённо: бонус, если король не на краю
        int kf = -1, kr = -1;
        for(int f=0;f<8;f++) for(int r=0;r<8;r++) {
            if (decode_type(b.cells[f][r]) == FigureType::KING && decode_color(b.cells[f][r]) == (for_white?'+':'-')) {
                kf = f; kr = r; break;
            }
        }
        if (kf < 0) return 0;
        int dist_center = std::abs(kf-3) + std::abs(kr-3);
        return (for_white ? 1 : -1) * (20 - dist_center * 3);
    }
    
    static int evaluate(const Board& b, bool for_white) {
        int score = material(b, for_white);
        score += mobility(b, for_white);
        score += center_control(b);
        score += king_safety(b, for_white);
        return for_white ? score : -score;
    }
    
    static int evaluate_endgame(const Board& b, bool for_white) {
        int score = material(b, for_white) * 2; // материал важнее
        // Бонус за проходные пешки (упрощённо)
        for(int f=0;f<8;f++) for(int r=0;r<8;r++) {
            int c = b.cells[f][r];
            if (decode_type(c) != FigureType::PAWN) continue;
            bool white = decode_color(c) == '+';
            if (white != for_white) continue;
            // Проверка: нет ли вражеских пешек на этом и соседних файлах впереди
            bool passed = true;
            int dir = white ? 1 : -1;
            for(int rf = r+dir; white ? rf<8 : rf>=0; rf += dir) {
                for(int df = std::max(0,f-1); df <= std::min(7,f+1); df++) {
                    int cc = b.cells[df][rf];
                    if (cc != 0 && decode_type(cc) == FigureType::PAWN && decode_color(cc) != (white?'+':'-')) {
                        passed = false; break;
                    }
                }
                if (!passed) break;
            }
            if (passed) score += white ? 30 : -30;
        }
        // Активность короля в эндшпиле
        score += king_safety(b, for_white) * 2;
        return for_white ? score : -score;
    }
};

const int Evaluator::VAL[7] = {0, 100, 320, 330, 500, 900, 20000};

// ============================================================================
// === ПОИСК (MINIMAX + ALPHA-BETA) ===
// ============================================================================

class Search {
public:
    static Move iterative_deepening(Board& b, bool for_white, int time_limit_ms) {
        Move best;
        auto start = std::chrono::steady_clock::now();
        
        for (int depth = 1; depth <= 8; depth++) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            if (elapsed > time_limit_ms * 0.8) break; // резерв 20%
            
            Move m = alphabeta_root(b, for_white, depth);
            if (m.valid()) best = m;
            
            debug("Depth " + std::to_string(depth) + " best: " + best.str());
        }
        return best.valid() ? best : fallback_move(b, for_white);
    }
    
private:
    static Move alphabeta_root(Board& b, bool for_white, int depth) {
        auto moves = MoveGen::generate(b, for_white);
        if (moves.empty()) return {};
        
        // Простая сортировка: сначала взятия
        std::sort(moves.begin(), moves.end(), [&b](const Move& a, const Move& b) {
            return (b.cells[Board::f(a.to.x)][Board::r(a.to.y)] != 0) > 
                   (b.cells[Board::f(b.to.x)][Board::r(b.to.y)] != 0);
        });
        
        Move best_move;
        int best_score = INT_MIN;
        int alpha = INT_MIN, beta = INT_MIN;
        
        for (const auto& m : moves) {
            Board nb = b;
            if (!apply_move(nb, m, for_white)) continue;
            int score = -alphabeta(nb, depth-1, -beta, -alpha, !for_white, for_white);
            if (score > best_score) {
                best_score = score;
                best_move = m;
            }
            alpha = std::max(alpha, score);
        }
        return best_move;
    }
    
    static int alphabeta(Board& b, int depth, int alpha, int beta, bool maximizing, bool bot_white) {
        if (depth == 0) {
            bool endgame = is_endgame(b);
            return endgame ? Evaluator::evaluate_endgame(b, bot_white) 
                          : Evaluator::evaluate(b, bot_white);
        }
        
        auto moves = MoveGen::generate(b, maximizing);
        if (moves.empty()) {
            // Проверка на мат/пат упрощённая
            return maximizing ? -100000 : 100000;
        }
        
        if (maximizing) {
            int max_eval = INT_MIN;
            for (const auto& m : moves) {
                Board nb = b;
                if (!apply_move(nb, m, maximizing)) continue;
                int eval = alphabeta(nb, depth-1, alpha, beta, false, bot_white);
                max_eval = std::max(max_eval, eval);
                alpha = std::max(alpha, eval);
                if (beta <= alpha) break;
            }
            return max_eval;
        } else {
            int min_eval = INT_MAX;
            for (const auto& m : moves) {
                Board nb = b;
                if (!apply_move(nb, m, !maximizing)) continue;
                int eval = alphabeta(nb, depth-1, alpha, beta, true, bot_white);
                min_eval = std::min(min_eval, eval);
                beta = std::min(beta, eval);
                if (beta <= alpha) break;
            }
            return min_eval;
        }
    }
    
    static bool apply_move(Board& b, const Move& m, bool for_white) {
        int& from_cell = b.at(m.from.x, m.from.y);
        int& to_cell = b.at(m.to.x, m.to.y);
        
        // Простая проверка: своя фигура -> чужая/пустая
        if (from_cell == 0) return false;
        if (decode_color(from_cell) != (for_white ? '+' : '-')) return false;
        
        // Взятие на проходе
        if (m.is_ep) {
            int cap_rank = Board::r(m.from.y) + (for_white ? -1 : 1);
            b.cells[Board::f(m.to.x)][cap_rank] = 0;
        }
        
        // Рокировка: перемещаем ладью
        if (m.is_castle) {
            int rank = Board::r(m.from.y);
            if (m.to.x == 'g') { // kingside
                b.cells[5][rank] = b.cells[7][rank];
                b.cells[7][rank] = 0;
            } else if (m.to.x == 'c') { // queenside
                b.cells[3][rank] = b.cells[0][rank];
                b.cells[0][rank] = 0;
            }
        }
        
        // Перемещение фигуры
        int piece = from_cell;
        if (m.promo != FigureType::QUEEN && decode_type(piece) == FigureType::PAWN) {
            piece = encode(for_white ? '+' : '-', m.promo);
        }
        to_cell = piece;
        from_cell = 0;
        
        // Обновление ep
        b.ep = {'\0','\0'};
        if (decode_type(piece) == FigureType::PAWN && std::abs(Board::r(m.to.y) - Board::r(m.from.y)) == 2) {
            b.ep = {m.from.x, Board::yf((Board::r(m.from.y) + Board::r(m.to.y)) / 2)};
        }
        
        // Обновление castling rights (упрощённо)
        if (decode_type(piece) == FigureType::KING) {
            if (for_white) b.castle[0] = b.castle[1] = false;
            else b.castle[2] = b.castle[3] = false;
        }
        if (decode_type(piece) == FigureType::ROOK) {
            if (for_white) {
                if (m.from.x == 'a' && m.from.y == '1') b.castle[1] = false;
                if (m.from.x == 'h' && m.from.y == '1') b.castle[0] = false;
            } else {
                if (m.from.x == 'a' && m.from.y == '8') b.castle[3] = false;
                if (m.from.x == 'h' && m.from.y == '8') b.castle[2] = false;
            }
        }
        
        b.white_to_move = !for_white;
        return true;
    }
    
    static bool is_endgame(const Board& b) {
        int pieces = 0;
        for(int f=0;f<8;f++) for(int r=0;r<8;r++) if (b.cells[f][r] != 0) pieces++;
        return pieces <= 10;
    }
    
    static Move fallback_move(const Board& b, bool for_white) {
        auto moves = MoveGen::generate(b, for_white);
        return moves.empty() ? Move() : moves[0];
    }
};

// ============================================================================
// === ДЕБЮТНАЯ КНИГА (ЗАПОЛНЯТЬ ЗДЕСЬ!) ===
// ============================================================================

class OpeningBook {
private:
    // Ключ: строка позиции (упрощённо: последовательность ходов)
    // Значение: ответный ход
    std::unordered_map<std::string, std::string> book;
    
public:
    OpeningBook() { load(); }
    
    void load() {
        // === ЗАПОЛНЯЙ ЭТУ ФУНКЦИЮ ДЛЯ ЛАБЫ 1 ===
        // Формат: "ключ_позиции" -> "ход_бота"
        
        // Пример для белых (бот играет +):
        book[""] = "e2e4";  // Стартовый ход
        book["e2e4"] = "e7e5";  // Если соперник ответил e5
        book["e2e4_e7e5"] = "g1f3"; // 2. Nf3
        book["e2e4_e7e5_g1f3_b8c6"] = "f1c4"; // 3. Bc4 (Итальянская партия)
        
        // Пример для чёрных (если бот играет -):
        // book["e2e4"] = "c7c5"; // Сицилианская защита
        
        // Добавь 10-20 ходов теории — этого хватит для демонстрации
        // Ключ можно формировать как конкатенацию всех сделанных ходов через "_"
    }
    
    std::string lookup(const std::string& position_key) {
        auto it = book.find(position_key);
        return (it != book.end()) ? it->second : "";
    }
    
    // Упрощённый ключ: просто список ходов из истории (нужно передавать извне)
    // Для лабы можно хранить историю в main() и формировать ключ там
};

// ============================================================================
// === ОСНОВНОЙ БОТ ===
// ============================================================================

class ChessBot {
private:
    bool bot_is_white;
    OpeningBook opening;
    std::string move_history; // для ключа дебютной книги
    
    std::string make_position_key() const {
        // Упрощённо: возвращаем историю ходов как ключ
        // В продакшене нужен хеш Зобриста
        return move_history;
    }
    
    bool is_opening(const Board& b) const {
        // Критерий: мало фигур сдвинулось с начальных позиций
        int moved = 0;
        for (int f = 0; f < 8; f++) {
            if (b.cells[f][1] != 0 && decode_type(b.cells[f][1]) != FigureType::PAWN) moved++;
            if (b.cells[f][6] != 0 && decode_type(b.cells[f][6]) != FigureType::PAWN) moved++;
        }
        return moved <= 4 && move_history.length() < 12; // ~6 полуходов
    }
    
    Move get_opening_move() {
        std::string key = make_position_key();
        std::string response = opening.lookup(key);
        if (!response.empty() && response.length() == 4) {
            Move m;
            m.from = {response[0], response[1]};
            m.to   = {response[2], response[3]};
            debug("Opening book: " + response);
            return m;
        }
        return Move(); // не нашли — переходим к поиску
    }
    
public:
    ChessBot(bool white) : bot_is_white(white) {}
    
    Move get_move(const InputData& in, int time_limit_ms = 3000) {
        Board b;
        b.load(in, bot_is_white);
        
        // Фаза 1: Дебют
        if (is_opening(b)) {
            Move m = get_opening_move();
            if (m.valid()) {
                move_history += (move_history.empty() ? "" : "_") + m.str();
                return m;
            }
        }
        
        // Фаза 2/3: Поиск
        bool endgame = true;
        int pieces = 0;
        for(int f=0;f<8;f++) for(int r=0;r<8;r++) if (b.cells[f][r]!=0) pieces++;
        endgame = (pieces <= 10);
        
        int depth = endgame ? 6 : 4; // глубже в эндшпиле
        Move best = Search::iterative_deepening(b, bot_is_white, time_limit_ms);
        
        // Обновляем историю (упрощённо)
        if (best.valid()) {
            move_history += (move_history.empty() ? "" : "_") + best.str();
        }
        
        return best;
    }
    
    void add_opponent_move(const std::string& opp_move) {
        move_history += (move_history.empty() ? "" : "_") + opp_move;
    }
};

// ============================================================================
// === MAIN ===
// ============================================================================

int main() {
    // Определяем цвет бота: если на входе есть фигуры на 1-2 рядах с '+', значит бот белый
    bool bot_is_white = true; // можно определить динамически при первом ходе
    
    ChessBot bot(bot_is_white);
    InputData in;
    
    while (true) {
        read_input(in);
        if (in.pong < 0) break; // сигнал завершения
        
        // Если это не первый ход и соперник только что походил — запоминаем его ход
        // (для дебютной книги). Упрощённо: берём последний ход из истории сервера
        // В реальности нужно сравнивать с предыдущей позицией
        
        Move best = bot.get_move(in, 4000); // 4 секунды на ход
        
        if (best.valid()) {
            output_move(best.from, best.to, in.pong);
            // После своего хода можно добавить его в историю для следующего ключа
            // bot.add_opponent_move(...) — но это делает сервер, не бот
        } else {
            // Заглушка: случайный легальный ход
            Board b; b.load(in, bot_is_white);
            auto moves = MoveGen::generate(b, bot_is_white);
            if (!moves.empty()) {
                Move rnd = moves[rand() % moves.size()];
                output_move(rnd.from, rnd.to, in.pong);
            }
        }
    }
    
    return 0;
}
