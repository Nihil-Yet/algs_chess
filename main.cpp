#include <iostream>
#include <vector>
#include <string>
#include <limits>

enum class FigureType {
    PAWN = 1,
    KNIGHT = 2,
    BISHOP = 3,
    ROOK = 4,
    QUEEN = 5,
    KING = 6
};

struct Cell {
    char x;  // 'a' - 'h'
    int y;   // 1 - 8
};

struct Figure {
    Cell position;
    char color;       // '+' или '-'
    FigureType type;
};

struct InputData {
    int pong;
    std::vector<Figure> figures;
};

void read_input(InputData* data);

int main()
{
    InputData idata;
    while (1) {
        read_input(&idata);
        if (idata.pong < 0) break;
    }
    return 0;
}

void read_input(InputData* data)
{
    int n;
    std::cin >> data->pong >> n;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    
    data->figures.resize(n);
    std::string line;
    for (int i = 0; i < n; i++) {
        std::getline(std::cin, line); 
        data->figures[i].position.x = line[0];
        data->figures[i].position.y = line[1] - '0';
        data->figures[i].color      = line[3];
        data->figures[i].type       = static_cast<FigureType>(line[5] - '0');
    }

    return;
}
