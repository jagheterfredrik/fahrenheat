#include <esp32-hal-ledc.h>

static const uint8_t CLKOUT = 10;

namespace Pwm
{
    void setup()
    {
        ledcSetClockSource(LEDC_USE_APB_CLK);
        // if (!ledcAttach(CLKOUT, 20000000, 1))
        // if (!ledcAttach(CLKOUT, 4000000, 1))
        if (!ledcAttach(CLKOUT, 40000000, 1))
        {
            printf("Clock setup failed\n");
        }
        ledcWrite(CLKOUT, 1);
    }
}
