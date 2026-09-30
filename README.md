# Spoofer de Serial de Disco — Estudo sobre HWID e o FiveM

> Estudo pessoal sobre como mecanismos de HWID (Hardware ID) identificam um PC e como um
> driver de kernel do Windows pode falsificar o serial dos discos para burlar essa identificação.
>
> **Atenção:** partes cruciais do código foram **removidas** para fins de estudo. O código deste
> repositório é didático — não funciona sem reimplementar o que falta.

## Contexto

Depois de trocar de placa-mãe e continuar sendo detectado, fiz um estudo para entender o que
exatamente identifica um PC para plataformas como o FiveM. Cheguei à conclusão de que os
principais vetores de identificação são:

| Vetor | O que é | Como falsifiquei |
|---|---|---|
| Serial dos discos | Identificador único do SSD/HDD | Driver de kernel (este estudo) |
| Endereço MAC | Identificador da placa de rede | Registro (`NetworkAddress`) + reinício do adaptador |
| Cache local | `ros_id.dat`, cache do CitizenFX | Limpeza das pastas |
| Conta | Vínculo da conta Cfx.re | Conta nova |
| Serial da GPU | Serial via NVAPI (NVIDIA) | Não falsifiquei (exige hook em usermode) |

Descobri que eu **não tinha adaptador Wi-Fi** (conexão via USB/tethering), então o MAC que
importava era o da Ethernet/USB, e que o serial dos **5 discos** era o vetor mais provável.

## Como o Windows expõe o serial de um disco

No Windows, toda operação de I/O assíncrona é um **IRP** (*I/O Request Packet*). Quando um
programa quer o serial de um disco, ele abre `\\.\PhysicalDriveN` e chama `DeviceIoControl`
com o código `IOCTL_STORAGE_QUERY_PROPERTY`. O IRP desce pela pilha de drivers de storage até
o driver de classe `\Driver\Disk` (disk.sys), que responde com um `STORAGE_DEVICE_DESCRIPTOR`
— dentro dele, `SerialNumberOffset` aponta para a string do serial.

É por esse caminho que o WMI (`Win32_DiskDrive`) — e consequentemente a maioria dos
anticheats, incluindo o do FiveM — lê o serial. Existe também um caminho legado via SMART
(`SMART_RCV_DRIVE_DATA`), cuja resposta carrega o bloco `IDENTIFY DEVICE` do padrão ATA
(struct `IDINFO`, campo `sSerialNumber` = words 10-19).

## Como o driver funciona

A técnica é um *man-in-the-middle* transparente na pilha de kernel:

1. **Hook da dispatch table** — no `DriverEntry`, o driver obtém o `DRIVER_OBJECT` de
   `\Driver\Disk` e substitui a entrada `MajorFunction[IRP_MJ_DEVICE_CONTROL]` pelo seu
   próprio handler, guardando o original para encadear.
2. **Filtro seletivo** — o handler olha o `IoControlCode` de cada IRP: se for
   `IOCTL_STORAGE_QUERY_PROPERTY` ou `SMART_RCV_DRIVE_DATA`, registra uma
   **completion routine** no IO stack location (com `SL_INVOKE_ON_SUCCESS`) e repassa o IRP
   para o driver original.
3. **Reescrita da resposta** — quando o disco responde e o IRP *volta* subindo a pilha, a
   completion routine roda **em cima da resposta já preenchida**: localiza o serial (via
   `SerialNumberOffset` no descriptor, ou `sSerialNumber` no bloco ATA) e reescreve o buffer
   *in place* com um serial falso, antes de entregar a resposta ao processo que perguntou.

```text
[Programa/FiveM]
      │  DeviceIoControl(IOCTL_STORAGE_QUERY_PROPERTY)
      ▼
[Driver Spoofer]  ── registra completion routine ──► [\Driver\Disk original]
      ▲                                              │ IRP desce...
      │  completion routine: reescreve o serial       ▼
[Programa/FiveM] ◄── resposta com serial falso ─── [disco físico (intocado)]
```

Pontos que tornam a abordagem discreta:

- **Não retorna erro nem esconde o disco** — respostas de erro são suspeitas para anticheat.
- **Encadeia a completion routine original** — se outro software registrou callback, ele roda.
- **Zero persistência** — nada é gravado em disco ou registro; o spoof some no reboot.

## Geração do serial falso

O gerador (`serial_gen.cpp`) preserva o formato do serial original (hex puro, ASCII, ou o
campo fixo de 20 chars do ATA), divide a string em duas metades intercaladas (caracteres
pares/ímpares) e randomiza cada metade com um `std::mt19937_64` semeado por um hash
**FNV-1a do serial original XOR o tempo do sistema** — determinístico dentro da sessão,
diferente a cada boot.

## Como verificar

```powershell
Get-CimInstance Win32_DiskDrive | Select-Object Model, SerialNumber
```

Para carregar um driver assinado com certificado de teste é preciso `bcdedit /set testsigning on`
e Secure Boot desativado na BIOS. O driver pode ser registrado como serviço de kernel:

```
sc create hwidshift type= kernel start= demand binPath= "C:\caminho\spoofer.sys"
sc start hwidshift
```

## Limitações

- Não cobre leitura de serial por caminhos fora do `disk.sys` (VPD page SCSI direto,
  `IOCTL_STORAGE_GET_DEVICE_TELEMETRY`, etc.).
- Não toca em GPU, SMBIOS, TPM ou MAC — cada um exige técnica própria.
- O serial reportado é igual para todos os discos na mesma sessão.

## Estado do código

O código em `Source/` está **incompleto de propósito**:

- `hwid.cpp` — a lógica de hook (obtenção do `DRIVER_OBJECT` e troca da dispatch table) foi
  removida e substituída por dicas nos comentários. As completion routines estão mantidas
  para estudo.
- `serial_gen.cpp` — completo (é apenas geração de string, inofensivo sozinho).
- `defs.h` — estruturas de dados do SMART (o que antigamente vinha em `<ntdddisk.h>`).

Base de estudo: projeto open source [HWID-shifter](https://github.com/AndrzejRPiotrowski/HWID-shifter),
de AndrzejPiotrowski. Recomendo ler o original depois de entender este.

## Aviso

Este repositório tem fins exclusivamente educacionais. Uso para burlar banimentos de
plataformas/anticheats é de responsabilidade de cada um.
