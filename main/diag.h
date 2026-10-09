#pragma once

// Diagnostika: hledani externich pull-up / pull-down rezistoru na volnych GPIO.
//
// Spusteni: drzet BtnC pri zapnuti / resetu / probuzeni pres BtnA. Vysledek se
// zive obnovuje na displeji i v seriove lince (lze prilozit rezistor a hned
// videt zmenu). BtnB nebo 5 min = konec, pak FW pokracuje normalne.
//
// Princip: na pinu se zapne interni pull-down (~45k), ADC zmeri napeti. Externi
// pull-up z nej udela delic -> R_ext ~ 45k * (3.3 - U) / U. Obracene s internim
// pull-upem pro pull-down. Interni odpor ma velky rozptyl (30-80k), hodnota je
// jen odhad; na rozhodnuti "je / neni" staci. Piny bez ADC jen digitalne
// (pull-up silnejsi nez ~15k). GPIO34-39 interni pull nemaji - jen napeti.

// Po M5.begin(): je drzene BtnC?
bool diagRequested();

void diagRun();
