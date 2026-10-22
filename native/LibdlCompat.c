/* Android 9 libdl exposes this loader API; the Android 10 libdl does not. */
void android_set_application_target_sdk_version(int target) {
    (void) target;
}

int android_get_application_target_sdk_version(void) {
    return 28;
}
