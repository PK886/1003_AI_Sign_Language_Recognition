# 1003_AI_Sign_Language_Recognition

1003 is based on `995_AI_Hand_Landmarks` and adds a first-stage sign-language recognition layer.

## Goal

Camera input -> hand landmarks -> basic sign classification -> English text on LCD.

Voice output and user-defined gesture recording are reserved as interfaces for the next stage.

## Supported Basic Signs

| Sign | LCD text | Rule summary |
| --- | --- | --- |
| Five fingers open | Hello | All fingers extended and spread |
| Index finger up | Yes | Only index finger extended upward |
| Thumb down | No | Only thumb extended downward |
| Closed fist | Help | No fingers extended |
| Open palm together | Stop | All fingers extended, fingertips close |
| Three fingers | Water | Index, middle and ring extended |
| OK sign | OK | Thumb and index pinch, other fingers open |
| Thumb up | Thanks | Only thumb extended upward |

## Added Files

- `Appli/Core/Inc/app_sign.h`
- `Appli/Core/Src/app_sign.c`
- `Appli/Core/Inc/app_voice.h`
- `Appli/Core/Src/app_voice.c`

## Reserved Interfaces

Voice output:

- `app_voice_init()`
- `app_voice_is_ready()`
- `app_voice_say_text(const char *text)`

User-defined gesture recording:

- `app_sign_user_record_begin(uint8_t slot, const char *text)`
- `app_sign_user_record_sample(const ld_point_t landmarks[LD_LANDMARKS_NB])`
- `app_sign_user_record_commit()`
- `app_sign_user_record_cancel()`

The current user-recording implementation is an API placeholder. It accepts samples and tracks recording state, but does not yet persist or match custom templates.

## Board Notes

According to the ALIENTEK DNN647 wiki, the hand-landmark example uses the MIPI camera on the base board `J2` connector and an RGB LCD connected to the core board `RGBLCD` connector. The example supports ALIENTEK RGB touch screens at `800x480`, which matches this project configuration.

The board also exposes USB UART and audio output resources, so the reserved voice layer can later be wired either to a serial voice module or to the board audio playback path.
