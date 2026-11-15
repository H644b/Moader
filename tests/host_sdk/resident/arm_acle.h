// Fail on ARM hosts too if this harness regresses to the full SDK umbrella.
// No ACLE declarations or implementations belong in the host lifecycle test.
#error "Resident host harness must not include ARM ACLE intrinsics"
