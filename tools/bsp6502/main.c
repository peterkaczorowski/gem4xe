/* Resident: bank $00 code that calls into banked code. */
int far_a(int x);
int far_b(int x);

int main(void) { return far_a(3) + far_b(4); }
