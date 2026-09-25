#include "Touch_SPD2010.h"

// ── Globals ───────────────────────────────────────────────────────────────────
struct SPD2010_Touch touch_data = {0};

// volatile is REQUIRED: this variable is written inside an ISR and read from
// the main loop. Without it the compiler may optimise the read away entirely.
volatile uint8_t Touch_interrupts = 0;

// ── I2C helpers ───────────────────────────────────────────────────────────────
bool I2C_Read_Touch(uint8_t Driver_addr, uint16_t Reg_addr,
                    uint8_t *Reg_data, uint32_t Length)
{
    Wire.beginTransmission(Driver_addr);
    Wire.write((uint8_t)(Reg_addr >> 8));
    Wire.write((uint8_t)(Reg_addr & 0xFF));

    // 'false' = repeated-start — keeps the bus open so we can read back
    if (Wire.endTransmission(false) != 0) {
        Serial.println("[TOUCH] I2C Read: endTransmission failed.");
        return false;
    }

    Wire.requestFrom((uint16_t)Driver_addr, (size_t)Length);
    for (uint32_t i = 0; i < Length; i++) {
        if (Wire.available()) {
            *Reg_data++ = Wire.read();
        }
    }
    return true;
}

bool I2C_Write_Touch(uint8_t Driver_addr, uint16_t Reg_addr,
                     const uint8_t *Reg_data, uint32_t Length)
{
    Wire.beginTransmission(Driver_addr);
    Wire.write((uint8_t)(Reg_addr >> 8));
    Wire.write((uint8_t)(Reg_addr & 0xFF));
    for (uint32_t i = 0; i < Length; i++) {
        Wire.write(*Reg_data++);
    }
    if (Wire.endTransmission(true) != 0) {
        Serial.println("[TOUCH] I2C Write: endTransmission failed.");
        return false;
    }
    return true;
}

// ── ISR ───────────────────────────────────────────────────────────────────────
void IRAM_ATTR Touch_SPD2010_ISR(void)
{
    // Set flag — the main loop / LVGL callback polls this before reading the IC
    Touch_interrupts = 1;
}

// ── Initialisation ────────────────────────────────────────────────────────────
uint8_t Touch_Init(void)
{
    SPD2010_Touch_Reset();
    SPD2010_Read_cfg();

    // Attach hardware interrupt — FALLING edge = touch controller asserts INT low
    pinMode(EXAMPLE_PIN_NUM_TOUCH_INT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(EXAMPLE_PIN_NUM_TOUCH_INT),
                    Touch_SPD2010_ISR, FALLING);

    Serial.println("[TOUCH] Touch_Init complete. Interrupt attached on GPIO4.");
    return 1;
}

uint8_t SPD2010_Touch_Reset(void)
{
    // EXIO_PIN1 is the touch-controller reset line via TCA9554
    Set_EXIO(EXIO_PIN1, Low);
    delay(50);
    Set_EXIO(EXIO_PIN1, High);
    delay(50);
    return 1;
}

uint16_t SPD2010_Read_cfg(void)
{
    read_fw_version();
    return 1;
}

// ── Data read ─────────────────────────────────────────────────────────────────
void Touch_Read_Data(void)
{
    struct SPD2010_Touch touch = {0};
    tp_read_data(&touch);

    // Update shared struct inside a critical section so LVGL never sees a
    // half-written value
    noInterrupts();
    uint8_t touch_cnt = (touch.touch_num > CONFIG_ESP_LCD_TOUCH_MAX_POINTS)
                        ? CONFIG_ESP_LCD_TOUCH_MAX_POINTS
                        : touch.touch_num;
    touch_data.touch_num = touch_cnt;
    for (int i = 0; i < touch_cnt; i++) {
        touch_data.rpt[i].x      = touch.rpt[i].x;
        touch_data.rpt[i].y      = touch.rpt[i].y;
        touch_data.rpt[i].weight = touch.rpt[i].weight;
    }
    interrupts();
}

bool Touch_Get_xy(uint16_t *x, uint16_t *y, uint16_t *strength,
                  uint8_t *point_num, uint8_t max_point_num)
{
    Touch_Read_Data();

    *point_num = (touch_data.touch_num > max_point_num)
                 ? max_point_num
                 : touch_data.touch_num;

    for (uint8_t i = 0; i < *point_num; i++) {
        x[i] = touch_data.rpt[i].x;
        y[i] = touch_data.rpt[i].y;
        if (strength) {
            strength[i] = touch_data.rpt[i].weight;
        }
    }

    // Clear count so the same event is not reported twice
    touch_data.touch_num = 0;
    return (*point_num > 0);
}

void example_touchpad_read(void)
{
    uint16_t tp_x = 0, tp_y = 0;
    uint8_t  tp_cnt = 0;
    if (Touch_Get_xy(&tp_x, &tp_y, NULL, &tp_cnt, 1) && tp_cnt > 0) {
        Serial.printf("[TOUCH] X=%d  Y=%d\n", tp_x, tp_y);
    }
}

void Touch_Loop(void)
{
    example_touchpad_read();
}

// ── Low-level SPD2010 protocol ────────────────────────────────────────────────
esp_err_t write_tp_point_mode_cmd(void)
{
    uint8_t data[2] = {0x00, 0x00};
    I2C_Write_Touch(SPD2010_ADDR, 0x5000, data, 2);
    esp_rom_delay_us(200);
    return ESP_OK;
}

esp_err_t write_tp_start_cmd(void)
{
    uint8_t data[2] = {0x00, 0x00};
    I2C_Write_Touch(SPD2010_ADDR, 0x4600, data, 2);
    esp_rom_delay_us(200);
    return ESP_OK;
}

esp_err_t write_tp_cpu_start_cmd(void)
{
    uint8_t data[2] = {0x01, 0x00};
    I2C_Write_Touch(SPD2010_ADDR, 0x0400, data, 2);
    esp_rom_delay_us(200);
    return ESP_OK;
}

esp_err_t write_tp_clear_int_cmd(void)
{
    uint8_t data[2] = {0x01, 0x00};
    I2C_Write_Touch(SPD2010_ADDR, 0x0200, data, 2);
    esp_rom_delay_us(200);
    return ESP_OK;
}

esp_err_t read_tp_status_length(tp_status_t *tp_status)
{
    uint8_t buf[4] = {0};
    I2C_Read_Touch(SPD2010_ADDR, 0x2000, buf, 4);
    esp_rom_delay_us(200);

    tp_status->status_low.pt_exist  = (buf[0] & 0x01);
    tp_status->status_low.gesture   = (buf[0] & 0x02);
    tp_status->status_low.aux       = (buf[0] & 0x08);
    tp_status->status_high.tic_busy   = ((buf[1] & 0x80) >> 7);
    tp_status->status_high.tic_in_bios= ((buf[1] & 0x40) >> 6);
    tp_status->status_high.tic_in_cpu = ((buf[1] & 0x20) >> 5);
    tp_status->status_high.tint_low   = ((buf[1] & 0x10) >> 4);
    tp_status->status_high.cpu_run    = ((buf[1] & 0x08) >> 3);
    tp_status->read_len = ((uint16_t)buf[3] << 8) | buf[2];
    return ESP_OK;
}

esp_err_t read_tp_hdp(tp_status_t *tp_status, SPD2010_Touch *touch)
{
    // 4 byte header + up to 10 fingers × 6 bytes each
    uint8_t buf[4 + (10 * 6)] = {0};
    I2C_Read_Touch(SPD2010_ADDR, 0x0003, buf, tp_status->read_len);

    uint8_t check_id = buf[4];

    if ((check_id <= 0x0A) && tp_status->status_low.pt_exist) {
        touch->touch_num = (tp_status->read_len - 4) / 6;
        touch->gesture   = 0x00;

        for (uint8_t i = 0; i < touch->touch_num; i++) {
            uint8_t off = i * 6;
            touch->rpt[i].id     = buf[4 + off];
            touch->rpt[i].x      = (((uint16_t)(buf[7 + off] & 0xF0) << 4) | buf[5 + off]);
            touch->rpt[i].y      = (((uint16_t)(buf[7 + off] & 0x0F) << 8) | buf[6 + off]);
            touch->rpt[i].weight = buf[8 + off];
        }

        // Slide gesture tracking
        if (touch->rpt[0].weight != 0 && touch->down != 1) {
            touch->down   = 1;  touch->up = 0;
            touch->down_x = touch->rpt[0].x;
            touch->down_y = touch->rpt[0].y;
        } else if (touch->rpt[0].weight == 0 && touch->down == 1) {
            touch->up   = 1;  touch->down = 0;
            touch->up_x = touch->rpt[0].x;
            touch->up_y = touch->rpt[0].y;
        }
    } else if ((check_id == 0xF6) && tp_status->status_low.gesture) {
        touch->touch_num = 0;
        touch->up = touch->down = 0;
        touch->gesture = buf[6] & 0x07;
    } else {
        touch->touch_num = 0;
        touch->gesture   = 0;
    }
    return ESP_OK;
}

esp_err_t read_tp_hdp_status(tp_hdp_status_t *tp_hdp_status)
{
    uint8_t buf[8] = {0};
    I2C_Read_Touch(SPD2010_ADDR, 0xFC02, buf, 8);
    tp_hdp_status->status          = buf[5];
    tp_hdp_status->next_packet_len = ((uint16_t)buf[3] << 8) | buf[2];
    return ESP_OK;
}

esp_err_t Read_HDP_REMAIN_DATA(tp_hdp_status_t *tp_hdp_status)
{
    uint8_t buf[32] = {0};
    I2C_Read_Touch(SPD2010_ADDR, 0x0003, buf, tp_hdp_status->next_packet_len);
    return ESP_OK;
}

esp_err_t read_fw_version(void)
{
    uint8_t buf[18] = {0};
    I2C_Read_Touch(SPD2010_ADDR, 0x2600, buf, 18);

    uint16_t DVer     = ((uint16_t)buf[5] << 8) | buf[4];
    uint32_t PID      = ((uint32_t)buf[9] << 24) | ((uint32_t)buf[8] << 16)
                       | ((uint32_t)buf[7] << 8)  | buf[6];
    uint32_t ICName_L = ((uint32_t)buf[13] << 24) | ((uint32_t)buf[12] << 16)
                       | ((uint32_t)buf[11] << 8)  | buf[10];   // "2010"
    uint32_t ICName_H = ((uint32_t)buf[17] << 24) | ((uint32_t)buf[16] << 16)
                       | ((uint32_t)buf[15] << 8)  | buf[14];   // "SPD"

    Serial.printf("[TOUCH] FW DVer=%u  PID=%u  IC=%u-%u\n",
                  DVer, PID, ICName_H, ICName_L);
    return ESP_OK;
}

esp_err_t tp_read_data(SPD2010_Touch *touch)
{
    tp_status_t     tp_status     = {0};
    tp_hdp_status_t tp_hdp_status = {0};

    read_tp_status_length(&tp_status);

    if (tp_status.status_high.tic_in_bios) {
        write_tp_clear_int_cmd();
        write_tp_cpu_start_cmd();
    } else if (tp_status.status_high.tic_in_cpu) {
        write_tp_point_mode_cmd();
        write_tp_start_cmd();
        write_tp_clear_int_cmd();
    } else if (tp_status.status_high.cpu_run && tp_status.read_len == 0) {
        write_tp_clear_int_cmd();
    } else if (tp_status.status_low.pt_exist || tp_status.status_low.gesture) {
        read_tp_hdp(&tp_status, touch);

    hdp_done_check:
        read_tp_hdp_status(&tp_hdp_status);
        if (tp_hdp_status.status == 0x82) {
            write_tp_clear_int_cmd();
        } else if (tp_hdp_status.status == 0x00) {
            Read_HDP_REMAIN_DATA(&tp_hdp_status);
            goto hdp_done_check;
        }
    } else if (tp_status.status_high.cpu_run && tp_status.status_low.aux) {
        write_tp_clear_int_cmd();
    }

    return ESP_OK;
}