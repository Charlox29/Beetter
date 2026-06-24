import 'package:flutter/material.dart';

// Matches the web design system (app/ and server/).
const kAmber = Color(0xFFF5CB5C);
const kAmberDark = Color(0xFFFFB300);
const kDarkSurface = Color(0xFF333533);
const kPageBg = Color(0xFFE8EDDF);
const kText = Color(0xFF242423);
const kMuted = Color(0xFF6B7280);

// Status colours
const kStatusOk = Color(0xFF639922);
const kStatusWarn = Color(0xFFF97316);
const kStatusCrit = Color(0xFFE24B4A);
const kStatusNone = Color(0xFF9CA3AF);

ThemeData buildTheme() {
  final base = ColorScheme.fromSeed(
    seedColor: kAmber,
    brightness: Brightness.light,
    primary: kAmberDark,
    onPrimary: Colors.black,
    secondary: kDarkSurface,
    onSecondary: Colors.white,
    surface: Colors.white,
    onSurface: kText,
  );

  return ThemeData(
    useMaterial3: true,
    colorScheme: base,
    scaffoldBackgroundColor: kPageBg,
    appBarTheme: const AppBarTheme(
      backgroundColor: kDarkSurface,
      foregroundColor: Colors.white,
      elevation: 0,
      titleTextStyle: TextStyle(
        color: Colors.white,
        fontSize: 17,
        fontWeight: FontWeight.w600,
      ),
      iconTheme: IconThemeData(color: kAmber),
    ),
    cardTheme: CardThemeData(
      color: Colors.white,
      elevation: 2,
      shadowColor: Colors.black12,
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
      margin: const EdgeInsets.all(0),
    ),
    elevatedButtonTheme: ElevatedButtonThemeData(
      style: ElevatedButton.styleFrom(
        backgroundColor: kAmber,
        foregroundColor: Colors.black,
        elevation: 0,
        padding: const EdgeInsets.symmetric(vertical: 14),
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
        textStyle: const TextStyle(fontWeight: FontWeight.w700, fontSize: 15),
      ),
    ),
    inputDecorationTheme: InputDecorationTheme(
      filled: true,
      fillColor: Colors.white,
      border: OutlineInputBorder(
        borderRadius: BorderRadius.circular(8),
        borderSide: const BorderSide(color: Color(0xFFD1D5DB)),
      ),
      enabledBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(8),
        borderSide: const BorderSide(color: Color(0xFFD1D5DB)),
      ),
      focusedBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(8),
        borderSide: const BorderSide(color: kAmberDark, width: 2),
      ),
      contentPadding: const EdgeInsets.symmetric(horizontal: 14, vertical: 13),
    ),
    chipTheme: ChipThemeData(
      backgroundColor: Colors.white,
      selectedColor: kAmber,
      labelStyle: const TextStyle(fontSize: 12, fontWeight: FontWeight.w600),
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 0),
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(6),
        side: const BorderSide(color: Color(0xFFD1D5DB)),
      ),
    ),
  );
}
