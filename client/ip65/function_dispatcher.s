;this is some very quick and dirty glue to make the most useful IP65 functions available via a single entry point.
;this allows user applications to be developed that don't link ip65 in directly, rather they use an instance of ip65 that is preloaded (or in a cartridge/ROM)
;this whole file could (and should) be greatly optimised by making it all table driven, but since this file is probably only going to be used in a bankswitched ROM where
;space is not at such a premium, I'll go with the gross hack for now.

.include "../inc/nb65_constants.i"
.include "../inc/common.i"
.include "../inc/commonprint.i"
.export nb65_dispatcher

.import ip65_init
.import dhcp_init
.import cs_driver_name
.import cfg_get_configuration_ptr
.import tftp_load_address
.importzp tftp_filename
.import tftp_ip
.import tftp_directory_listing
.import ip65_error
.import tftp_clear_callbacks
.import tftp_download
.import dns_ip
.import dns_resolve
.import dns_set_hostname
.import udp_callback
.import udp_add_listener
.import ip_inp
.import udp_inp
.import udp_send
.import udp_send_src
.import udp_send_src_port
.import udp_send_dest
.import udp_send_dest_port
.import udp_send_len
.import copymem
.import cfg_mac
.importzp copy_src
.importzp copy_dest

.zeropage
nb65_params:		.res 2

.data
jmp_old_irq:
  jmp $0000

irq_handler_installed_flag:
  .byte 0
  
.code

irq_handler:
  jsr NB65_VBL_VECTOR
  jmp jmp_old_irq


set_tftp_params:
  ldy #NB65_TFTP_IP
  lda (nb65_params),y
  sta tftp_ip
  iny
  lda (nb65_params),y
  sta tftp_ip+1
  iny
  lda (nb65_params),y
  sta tftp_ip+2
  iny
  lda (nb65_params),y
  sta tftp_ip+3

  ldy #NB65_TFTP_FILENAME
  lda (nb65_params),y
  sta tftp_filename
  iny
  lda (nb65_params),y
  sta tftp_filename+1

  ldy #NB65_TFTP_POINTER
  lda (nb65_params),y
  sta tftp_load_address
  iny
  lda (nb65_params),y
  sta tftp_load_address+1
  
  jsr tftp_clear_callbacks
  
  clc
  rts

nb65_dispatcher:
  stax nb65_params
  
  
  cpy #NB65_GET_DRIVER_NAME
  bne :+
  ldax  #cs_driver_name
  clc
  rts
:

  cpy #NB65_GET_IP_CONFIG
  bne :+
  stax  copy_dest
  ldax  #cfg_mac
  stax  copy_src
  ldax  #NB65_CFG_DHCP_SERVER+4 ;bytes to copy
  jsr copymem
  clc
  ldax nb65_params
  rts
:

  cpy #NB65_SET_IP_CONFIG
  bne :+
  stax  copy_src
  ldax  #cfg_mac
  stax  copy_dest
  ldax  #NB65_CFG_DHCP_SERVER+4 ;bytes to copy
  jsr copymem
  clc
  ldax nb65_params
  rts
:

  cpy #NB65_INIT_IP
  bne :+
  lda irq_handler_installed_flag
  bne irq_handler_installed
  ;install our IRQ handler
  ldax  $314    ;previous IRQ handler
  stax  jmp_old_irq+1
  sei ;don't want any interrupts while we fiddle with the vector
  ldax #irq_handler
  stax  $314    ;previous IRQ handler
  cli
  sta irq_handler_installed_flag
irq_handler_installed:  
  jmp ip65_init
:

  cpy #NB65_INIT_DHCP
  bne :+
  jmp dhcp_init
:

  cpy #NB65_TFTP_DIRECTORY_LISTING  
  bne :+
  jsr set_tftp_params
  bcs @tftp_error
  jsr tftp_directory_listing

@after_tftp_call:  ;write the current load address back to the param buffer (so if $0000 was passed in, the caller can find out the actual value used)
  bcs @tftp_error
  ldy #NB65_TFTP_POINTER
  lda tftp_load_address
  sta (nb65_params),y  
  iny
  lda tftp_load_address+1
  sta (nb65_params),y  
  clc
@tftp_error:
@dns_error:   
  rts
:

  cpy #NB65_TFTP_DOWNLOAD
  bne :+
  jsr set_tftp_params
  bcs @tftp_error
  jsr tftp_download
  jmp @after_tftp_call
:

  cpy #NB65_DNS_RESOLVE_HOSTNAME
  bne :+  
  ldy #NB65_DNS_HOSTNAME+1
  lda (nb65_params),y
  tax
  dey
  lda (nb65_params),y
  jsr dns_set_hostname  
  bcs @dns_error
  jsr dns_resolve
  bcs @dns_error
  ldy #NB65_DNS_HOSTNAME_IP
  ldx #4
@copy_dns_ip:
  lda dns_ip,y
  sta (nb65_params),y
  iny
  dex  
  bne @copy_dns_ip
  rts
:

  cpy #NB65_UDP_ADD_LISTENER
  bne :+  
  ldy #NB65_UDP_LISTENER_CALLBACK
  lda (nb65_params),y
  sta udp_callback
  iny
  lda (nb65_params),y
  sta udp_callback+1
  ldy #NB65_UDP_LISTENER_PORT+1
  lda (nb65_params),y
  tax
  dey
  lda (nb65_params),y
  
  jmp udp_add_listener
:

  cpy #NB65_GET_INPUT_PACKET_INFO
  bne :+
  ldy #3
@copy_src_ip:  
  lda ip_inp+12,y  ;src IP 
  sta (nb65_params),y
  dey
  bpl @copy_src_ip
  
  ldy #NB65_REMOTE_PORT
  lda udp_inp+1 ;src port (lo byte)
  sta (nb65_params),y
  iny
  lda udp_inp+0 ;src port (high byte)
  sta (nb65_params),y
  iny
  lda udp_inp+3 ;dest port (lo byte)
  sta (nb65_params),y
  iny
  lda udp_inp+2 ;dest port (high byte)
  sta (nb65_params),y

  iny
  sec
  lda udp_inp+5 ;payload length (lo byte)
  sbc #8  ;to remove length of header
  sta (nb65_params),y

  iny
  lda udp_inp+4 ;payload length (hi byte)
  sbc #0  ;in case there was a carry from the lo byte
  sta (nb65_params),y
  
  iny
  lda #<(udp_inp+8) ;payload ptr (lo byte)
  sta (nb65_params),y

  iny
  lda #>(udp_inp+8) ;payload ptr (hi byte)
  sta (nb65_params),y

  clc
  rts
:  

  cpy #NB65_SEND_UDP_PACKET
  bne :+
  ldy #3
@copy_dest_ip:  
  lda (nb65_params),y
  sta udp_send_dest,y
  dey
  bpl @copy_dest_ip
  
  ldy #NB65_REMOTE_PORT  
  lda (nb65_params),y
  sta udp_send_dest_port
  iny
  lda (nb65_params),y
  sta udp_send_dest_port+1
  iny

  lda (nb65_params),y
  sta udp_send_src_port
  iny
  lda (nb65_params),y
  sta udp_send_src_port+1
  iny


  lda (nb65_params),y
  sta udp_send_len
  iny
  lda (nb65_params),y
  sta udp_send_len+1
  iny

  ;AX should point at data to send
  lda (nb65_params),y
  pha
  iny
  lda (nb65_params),y  
  tax
  pla
  jmp udp_send
:  

  cpy #NB65_UNHOOK_VBL_IRQ
  bne :+
  ldax  jmp_old_irq+1
  sei ;don't want any interrupts while we fiddle with the vector
  stax  $314    ;previous IRQ handler
  cli
  clc
  rts
:  

  cpy #NB65_PRINT_ASCIIZ
  bne :+
  jsr print
  clc
  rts
:  

  cpy #NB65_PRINT_HEX
  bne :+
  jsr print_hex
  clc
  rts
:  

  cpy #NB65_PRINT_DOTTED_QUAD
  bne :+
  jsr print_dotted_quad
  clc
  rts
:  

  cpy #NB65_PRINT_IP_CONFIG
  bne :+
  jsr print_ip_config
  clc
  rts
:


  cpy #NB65_GET_LAST_ERROR
  bne :+
  lda ip65_error
  clc
  rts
:  


;default function handler
  lda #NB65_ERROR_FUNCTION_NOT_SUPPORTED
  sta ip65_error
  sec        ;carry flag set = error
  rts