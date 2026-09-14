#include <Wire.h>

// I2C Scanner - corre isto primeiro para descobrires em que endereço
// o ATECC608 esta a responder agora. Se a escrita anterior mudou o
// byte 16 (I2C_Address) para 0x83, o chip pode estar a responder
// perto de 0x41 em vez de 0x60.
//
// NOTA: o ATECC608 esta normalmente adormecido (Sleep) e so acorda
// com uma sequencia de Wake especifica (nao apenas um pedido I2C
// generico). Um scanner comum pode nao o "acordar" corretamente,
// por isso o mais fiavel e tentar begin() da biblioteca com cada
// endereco candidato encontrado, nao confiar cegamente no scan.

void setup() {
  Serial.begin(9600);
  while (!Serial);
  delay(1000);
  Serial.println("A fazer scan do barramento I2C...");

  int found = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.begin();
    Wire.beginTransmission(addr);
    byte error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("Dispositivo encontrado no endereco 0x");
      if (addr < 16) Serial.print("0");
      Serial.println(addr, HEX);
      found++;
    }
  }

  if (found == 0) {
    Serial.println("Nenhum dispositivo encontrado. O chip pode estar "
                    "adormecido (Sleep) - o scanner generico nao o acorda.");
    Serial.println("Experimenta correr o sketch original de teste (com "
                    "ECCX08.begin()) mas forcando manualmente enderecos "
                    "candidatos, ex: 0x41, 0x42, etc.");
  }
}

void loop() {}