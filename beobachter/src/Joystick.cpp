#include "Joystick.h"

Joystick::Joystick(unsigned pinX, unsigned pinY) : m_PinX(pinX), m_PinY(pinY) {}

void Joystick::init()
{
    pinMode(m_PinX, INPUT);
    pinMode(m_PinY, INPUT);
    m_PosX0 = analogRead(m_PinX);
    m_PosY0 = analogRead(m_PinY);
}

int Joystick::x()
{
    return analogRead(m_PinX) - m_PosX0;
}

int Joystick::y()
{
    return analogRead(m_PinY) - m_PosY0;
}

std::pair<int, int> Joystick::read()
{
    return std::make_pair(x(), y());
}