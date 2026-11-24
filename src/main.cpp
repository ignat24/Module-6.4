
#include <Arduino.h>

class CONFIG {
public:
    static constexpr uint8_t BUTTON_PIN = 0;
    static constexpr uint8_t LED_RED = 17;
    static constexpr uint8_t LED_WHITE = 16;

    static constexpr uint32_t POLL_MS = 10;
    static constexpr uint32_t DEBOUNCE_MS = 50;
    static constexpr uint32_t LONG_PRESS_MS = 1000;
    static constexpr uint32_t TIMEOUT_MS = 3000;

    static constexpr uint32_t QUEUE_SIZE = 10;
    static constexpr uint32_t STACK_SIZE = 4096;

    static constexpr UBaseType_t PRODUCER_PRIORITY = 1;
    static constexpr UBaseType_t CONSUMER_PRIORITY = 2;
};

enum class ButtonEvent {
    PressDown,
    Short,
    Long
};

enum class SafeState {
    Idle,
    FirstShort,
    SecondShort,
    Unlocked
};

class LED {
private:
    uint8_t pin;

public:
    constexpr explicit LED(uint8_t ledPin)
        : pin(ledPin) {}

    void init() {
        pinMode(pin, OUTPUT);
        off();
    }

    void on() {
        digitalWrite(pin, HIGH);
    }

    void off() {
        digitalWrite(pin, LOW);
    }
};

static LED red(CONFIG::LED_RED);
static LED white(CONFIG::LED_WHITE);

static QueueHandle_t buttonQueue = nullptr;

void sendEvent(ButtonEvent event) {
    xQueueSend(buttonQueue, &event, portMAX_DELAY);
}

void buttonTask(void* parameter) {
    bool stableState = HIGH;
    bool lastRawState = HIGH;
    bool longSent = false;

    TickType_t lastChangeTime = xTaskGetTickCount();
    TickType_t pressTime = 0;
    TickType_t lastWakeTime = xTaskGetTickCount();

    while (true) {
        TickType_t now = xTaskGetTickCount();

        bool rawState = digitalRead(CONFIG::BUTTON_PIN);

        if (rawState != lastRawState) {
            lastRawState = rawState;
            lastChangeTime = now;
        }

        if (rawState != stableState &&
            now - lastChangeTime >=
                pdMS_TO_TICKS(CONFIG::DEBOUNCE_MS)) {

            stableState = rawState;

            if (stableState == LOW) {
                pressTime = now;
                longSent = false;

                sendEvent(ButtonEvent::PressDown);
            }

            else if (!longSent) {
                if (now - pressTime >=
                    pdMS_TO_TICKS(CONFIG::LONG_PRESS_MS)) {

                    sendEvent(ButtonEvent::Long);
                    longSent = true;

                } else {
                    sendEvent(ButtonEvent::Short);
                }
            }
        }

        if (stableState == LOW &&
            rawState == LOW &&
            !longSent &&
            now - pressTime >=
                pdMS_TO_TICKS(CONFIG::LONG_PRESS_MS)) {

            sendEvent(ButtonEvent::Long);
            longSent = true;
        }

        vTaskDelayUntil(
            &lastWakeTime,
            pdMS_TO_TICKS(CONFIG::POLL_MS)
        );
    }
}

void safeTask(void* parameter) {
    SafeState state = SafeState::Idle;
    ButtonEvent event;

    red.on();
    white.off();

    Serial.println("[Safe] Enter password");

    while (true) {

        TickType_t timeout =
            state == SafeState::Unlocked
                ? portMAX_DELAY
                : pdMS_TO_TICKS(CONFIG::TIMEOUT_MS);

        if (xQueueReceive(
                buttonQueue,
                &event,
                timeout
            ) != pdPASS) {

            if (state != SafeState::Idle) {
                Serial.println("[Safe] Timeout. Reset");

                state = SafeState::Idle;

                red.on();
                white.off();
            }

            continue;
        }

        if (state == SafeState::Unlocked) {
            continue;
        }

        if (event == ButtonEvent::PressDown) {
            continue;
        }

        switch (state) {

            case SafeState::Idle:

                if (event == ButtonEvent::Short) {
                    state = SafeState::FirstShort;

                    Serial.println("[Safe] Step 1/3 OK");
                } else {
                    Serial.println("[Safe] Wrong password. Reset");
                }

                break;

            case SafeState::FirstShort:

                if (event == ButtonEvent::Short) {
                    state = SafeState::SecondShort;

                    Serial.println("[Safe] Step 2/3 OK");
                } else {
                    state = SafeState::Idle;

                    Serial.println("[Safe] Wrong password. Reset");
                }

                break;

            case SafeState::SecondShort:

                if (event == ButtonEvent::Long) {
                    state = SafeState::Unlocked;

                    red.off();
                    white.on();

                    Serial.println("[Safe] ACCESS GRANTED");
                } else {
                    state = SafeState::Idle;

                    Serial.println("[Safe] Wrong password. Reset");
                }

                break;

            case SafeState::Unlocked:
                break;
        }
    }
}

void setup() {
    Serial.begin(115200);

    pinMode(CONFIG::BUTTON_PIN, INPUT_PULLUP);

    red.init();
    white.init();

    buttonQueue = xQueueCreate(
        CONFIG::QUEUE_SIZE,
        sizeof(ButtonEvent)
    );

    if (buttonQueue == nullptr) {
        Serial.println("[ERROR] Queue creation failed");
        return;
    }

    if (xTaskCreate(
        safeTask,
        "Safe Consumer",
        CONFIG::STACK_SIZE,
        nullptr,
        CONFIG::CONSUMER_PRIORITY,
        nullptr
    ) != pdPASS) {
        Serial.println("[ERROR] Consumer creation failed");
        return;
    }

    if (xTaskCreate(
        buttonTask,
        "Button Producer",
        CONFIG::STACK_SIZE,
        nullptr,
        CONFIG::PRODUCER_PRIORITY,
        nullptr
    ) != pdPASS) {
        Serial.println("[ERROR] Producer creation failed");
    }
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
