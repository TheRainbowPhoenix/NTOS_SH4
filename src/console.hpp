#pragma once
#include <cstddef>
// Fixed storage console. Paint callbacks replace a whole row, including its tail.
template <unsigned Columns = 40, unsigned Rows = 40> class TextConsole {
    char lines_[Rows][Columns + 1]{};
    unsigned row_ = 0, column_ = 0;
public:
    unsigned row() const { return row_; }
    const char *line(unsigned row) const { return lines_[row]; }
    void reset() {
        for (auto &line : lines_) for (char &c : line) c = 0;
        row_ = column_ = 0;
    }
    void position(unsigned row) { row_ = row < Rows ? row : Rows - 1; column_ = 0; }
    template <class Paint, class Scroll> void newline(Paint paint, Scroll scroll) {
        column_ = 0;
        if (++row_ == Rows) {
            for (unsigned y = 1; y < Rows; ++y)
                for (unsigned x = 0; x <= Columns; ++x) lines_[y - 1][x] = lines_[y][x];
            row_ = Rows - 1;
            for (char &c : lines_[row_]) c = 0;
            if (scroll()) paint(row_, lines_[row_]);
            else for (unsigned y = 0; y < Rows; ++y) paint(y, lines_[y]);
        } else {
            for (char &c : lines_[row_]) c = 0;
        }
    }
    template <class Paint> void puts(const char *text, Paint paint) {
        puts(text, paint, [] { return false; });
    }
    template <class Paint, class Scroll> void puts(const char *text, Paint paint, Scroll scroll) {
        if (!text) return;
        bool ended = false;
        unsigned budget = 8192;
        while (*text && budget--) {
            unsigned char c = static_cast<unsigned char>(*text++);
            if (c == '\r') continue;
            if (c == '\n') { paint(row_, lines_[row_]); newline(paint, scroll); ended = true; continue; }
            if (column_ == Columns) { paint(row_, lines_[row_]); newline(paint, scroll); }
            lines_[row_][column_++] = c >= 32 && c < 127 ? static_cast<char>(c) : '?';
            lines_[row_][column_] = 0;
            ended = false;
        }
        paint(row_, lines_[row_]);
        if (!ended) newline(paint, scroll);
    }
};
