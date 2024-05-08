#include <Arduino.h>

class Joystick
{
    unsigned m_PinX, m_PinY;
    int m_PosX0 = 0, m_PosY0 = 0;

public:
    Joystick(unsigned pinX, unsigned pinY);

    void init();

    std::pair<int, int> read();

    int x();

    int y();
};