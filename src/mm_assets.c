/*
 * Weak fallbacks for generated asset symbols.
 * If xxd emits alternate names, these keep canonical names linkable.
 */
unsigned char icon_png[1] __attribute__((weak)) = {0};
unsigned int icon_png_len __attribute__((weak)) = 0;

unsigned char assets_icon_png[1] __attribute__((weak)) = {0};
unsigned int assets_icon_png_len __attribute__((weak)) = 0;
