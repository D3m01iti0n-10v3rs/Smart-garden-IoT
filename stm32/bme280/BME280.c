#include "BME280.h"

#define TIMEOUT 100

static I2C_HandleTypeDef *_hi2c;
static uint8_t _addr;

static uint16_t T1; static int16_t T2, T3;
static uint16_t P1; static int16_t P2, P3, P4, P5, P6, P7, P8, P9;
static uint8_t  H1, H3; static int16_t H2, H4, H5; static int8_t H6;

/* ------------------------------------------------------------------ */

static void write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    HAL_I2C_Master_Transmit(_hi2c, _addr, buf, 2, TIMEOUT);
}

static void read_reg(uint8_t reg, uint8_t *buf, uint8_t len)
{
    HAL_I2C_Master_Transmit(_hi2c, _addr, &reg, 1, TIMEOUT);
    HAL_I2C_Master_Receive(_hi2c, _addr, buf, len, TIMEOUT);
}

/* ------------------------------------------------------------------ */

void BME280_Init(I2C_HandleTypeDef *hi2c, uint8_t addr)
{
    uint8_t b[24];
    _hi2c = hi2c;
    _addr = addr;

    write_reg(0xE0, 0xB6);  /* reset */
    HAL_Delay(10);

    /* Temp + pressure calibration: 0x88..0x9F */
    read_reg(0x88, b, 24);
    T1 = (uint16_t)(b[1]  << 8 | b[0]);  T2 = (int16_t)(b[3]  << 8 | b[2]);
    T3 = (int16_t) (b[5]  << 8 | b[4]);  P1 = (uint16_t)(b[7] << 8 | b[6]);
    P2 = (int16_t) (b[9]  << 8 | b[8]);  P3 = (int16_t)(b[11] << 8 | b[10]);
    P4 = (int16_t) (b[13] << 8 | b[12]); P5 = (int16_t)(b[15] << 8 | b[14]);
    P6 = (int16_t) (b[17] << 8 | b[16]); P7 = (int16_t)(b[19] << 8 | b[18]);
    P8 = (int16_t) (b[21] << 8 | b[20]); P9 = (int16_t)(b[23] << 8 | b[22]);

    /* H1 is alone at 0xA1 */
    read_reg(0xA1, b, 1);
    H1 = b[0];

    /* Humidity calibration: 0xE1..0xE7 */
    read_reg(0xE1, b, 7);
    H2 = (int16_t)(b[1] << 8 | b[0]);
    H3 = b[2];
    H4 = (int16_t)((int8_t)b[3] << 4 | (b[4] & 0x0F));
    H5 = (int16_t)((int8_t)b[5] << 4 | (b[4] >> 4));
    H6 = (int8_t)b[6];

    write_reg(0xF2, 0x01);  /* humidity OSR x1 — must be before ctrl_meas */
    write_reg(0xF4, 0x27);  /* temp+pressure OSR x1, normal mode          */
    write_reg(0xF5, 0xA0);  /* standby 1000ms, filter off                 */
}

/* ------------------------------------------------------------------ */

BME280_Data BME280_Read(void)
{
    BME280_Data d = {0};
    uint8_t b[8];
    read_reg(0xF7, b, 8);

    int32_t raw_p = ((int32_t)b[0] << 12) | ((int32_t)b[1] << 4) | (b[2] >> 4);
    int32_t raw_t = ((int32_t)b[3] << 12) | ((int32_t)b[4] << 4) | (b[5] >> 4);
    int32_t raw_h = ((int32_t)b[6] <<  8) |  (int32_t)b[7];

    /* Temperature (sets t_fine, must run first) */
    float var1 = (raw_t / 16384.0f - T1 / 1024.0f) * T2;
    float var2 = (raw_t / 131072.0f - T1 / 8192.0f) *
                 (raw_t / 131072.0f - T1 / 8192.0f) * T3;
    int32_t t_fine = (int32_t)(var1 + var2);
    d.temperature = (var1 + var2) / 5120.0f;

    /* Pressure */
    var1 = t_fine / 2.0f - 64000.0f;
    var2 = var1 * var1 * P6 / 32768.0f + var1 * P5 * 2.0f;
    var2 = var2 / 4.0f + P4 * 65536.0f;
    var1 = (P3 * var1 * var1 / 524288.0f + P2 * var1) / 524288.0f;
    var1 = (1.0f + var1 / 32768.0f) * P1;
    if (var1 != 0.0f) {
        float p = (1048576.0f - raw_p - var2 / 4096.0f) * 6250.0f / var1;
        var1 = P9 * p * p / 2147483648.0f;
        var2 = p * P8 / 32768.0f;
        d.pressure = (p + (var1 + var2 + P7) / 16.0f) / 100.0f;
    }

    /* Humidity */
    float h = t_fine - 76800.0f;
    h = (raw_h - (H4 * 64.0f + H5 / 16384.0f * h)) *
        (H2 / 65536.0f * (1.0f + H6 / 67108864.0f * h *
        (1.0f + H3 / 67108864.0f * h)));
    h *= (1.0f - H1 * h / 524288.0f);
    if (h > 100.0f) h = 100.0f;
    if (h <   0.0f) h = 0.0f;
    d.humidity = h;

    return d;
}
