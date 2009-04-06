;test the "NETBOOT65 Cartridge API"
.include "../inc/nb65_constants.i"
 
; load A/X macro
	.macro ldax arg
	.if (.match (.left (1, arg), #))	; immediate mode
	lda #<(.right (.tcount (arg)-1, arg))
	ldx #>(.right (.tcount (arg)-1, arg))
	.else					; assume absolute or zero page
	lda arg
	ldx 1+(arg)
	.endif
	.endmacro

; store A/X macro
.macro stax arg
	sta arg
	stx 1+(arg)
.endmacro	

print_a = $ffd2

.macro cout arg
  lda arg
  jsr print_a
.endmacro   
    
  .zeropage
  temp_ptr:		.res 2
  
  .bss
  nb65_param_buffer: .res $20  

.segment "STARTUP"    ;this is what gets put at the start of the file on the C64

.word basicstub		; load address

.macro print arg
  ldax arg
	ldy #NB65_PRINT_ASCIIZ
  jsr NB65_DISPATCH_VECTOR 
.endmacro 

.macro print_cr
  lda #13
	jsr print_a
.endmacro

.macro call arg
	ldy arg
  jsr NB65_DISPATCH_VECTOR   
.endmacro

basicstub:
	.word @nextline
	.word 2003
	.byte $9e 
	.byte <(((init / 1000) .mod 10) + $30)
	.byte <(((init / 100 ) .mod 10) + $30)
	.byte <(((init / 10  ) .mod 10) + $30)
	.byte <(((init       ) .mod 10) + $30)
	.byte 0
@nextline:
	.word 0

init:
  
  lda #$01    
  sta $de00   ;turns on RR cartridge (since it will have been banked out when exiting to BASIC)

  ldy #NB65_GET_DRIVER_NAME
  jsr NB65_DISPATCH_VECTOR 
  
  ldy #NB65_PRINT_ASCIIZ
  jsr NB65_DISPATCH_VECTOR

  print #initializing

  ldy #NB65_INIT_IP
  jsr NB65_DISPATCH_VECTOR 
	bcc :+  
  print #failed
  jsr print_errorcode
  jmp bad_boot    
:  

  print #ok
  print_cr
  
  print #dhcp
  print #initializing
  
  call #NB65_INIT_DHCP  

	bcc :+  
  print #failed
  jsr print_errorcode
  jmp bad_boot    
:
 
  print #ok
  print_cr
  call #NB65_PRINT_IP_CONFIG
  
;DNS resolution test 
  
  ldax #test_hostname
  stax nb65_param_buffer+NB65_DNS_HOSTNAME

  call #NB65_PRINT_ASCIIZ  

  cout #' '
  cout #':'
  cout #' '
  
  ldax  #nb65_param_buffer
  call #NB65_DNS_RESOLVE_HOSTNAME  
  bcc :+
  print #dns_lookup_failed_msg
  print_cr
  jmp print_errorcode
:  
  ldax #nb65_param_buffer+NB65_DNS_HOSTNAME_IP
  call #NB65_PRINT_DOTTED_QUAD
  print_cr

;callback test
  
  ldax  #64     ;listen on port 64
  stax nb65_param_buffer+NB65_UDP_LISTENER_PORT
  ldax  #udp_callback
  stax nb65_param_buffer+NB65_UDP_LISTENER_CALLBACK
  ldax  #nb65_param_buffer
  call   #NB65_UDP_ADD_LISTENER
	bcc :+  
  print #failed
  jsr print_errorcode
  jmp bad_boot    
:

  print #listening	
  

@loop_forever:
  jsr NB65_PERIODIC_PROCESSING_VECTOR
  jmp @loop_forever
  
udp_callback:

  ldax #nb65_param_buffer
  call #NB65_GET_INPUT_PACKET_INFO

  print #port

  lda nb65_param_buffer+NB65_LOCAL_PORT+1
  call #NB65_PRINT_HEX

  lda nb65_param_buffer+NB65_LOCAL_PORT
  call #NB65_PRINT_HEX

  print_cr

  print #recv_from

  ldax #nb65_param_buffer+NB65_REMOTE_IP
  call #NB65_PRINT_DOTTED_QUAD
  
  cout #' '
  
  print #port
  
  lda nb65_param_buffer+NB65_REMOTE_PORT+1
  call #NB65_PRINT_HEX  
  lda nb65_param_buffer+NB65_REMOTE_PORT
  call #NB65_PRINT_HEX
  
  print_cr
  
  print #length

  lda nb65_param_buffer+NB65_PAYLOAD_LENGTH+1
  call #NB65_PRINT_HEX
  lda nb65_param_buffer+NB65_PAYLOAD_LENGTH
  call #NB65_PRINT_HEX
  print_cr  
  print #data

  ldax nb65_param_buffer+NB65_PAYLOAD_POINTER
  
  stax temp_ptr
  ldx nb65_param_buffer+NB65_PAYLOAD_LENGTH ;assumes length is < 255
  ldy #0
:
  lda (temp_ptr),y
  jsr print_a
  iny
  dex
  bpl :-
  
  print_cr

;make and send reply
  ldax #reply_message
  stax nb65_param_buffer+NB65_PAYLOAD_POINTER

  ldax #reply_message_length
  stax nb65_param_buffer+NB65_PAYLOAD_LENGTH
 
  ldax #nb65_param_buffer
  call #NB65_SEND_UDP_PACKET  
  bcc :+
  jmp print_errorcode
:
  print #reply_sent
  rts
  
bad_boot:
  print  #press_a_key_to_continue
  jsr get_key
  jmp $fce2   ;do a cold start


print_errorcode:
  print #error_code
  call #NB65_GET_LAST_ERROR
  call #NB65_PRINT_HEX
  print_cr
  rts

;use C64 Kernel ROM function to read a key
;inputs: none
;outputs: A contains ASCII value of key just pressed
get_key:
  jsr $ffe4
  cmp #0
  beq get_key
  rts
  
	.rodata
test_hostname:
  .byte "RETROHACKERS.COM",0          ;this should be an A record

recv_from:  
  .asciiz "RECEIVED FROM: "
  
listening:  
  .byte "LISTENING ON UDP PORT 64",13,0


reply_sent:  
  .byte "REPLY SENT.",13,0


initializing:  
  .byte " INITIALIZING ",0

dhcp:
.byte "DHCP",0

port:  
  .byte "PORT: $",0

length:  
  .byte "LENGTH: $",0
  
data:
  .byte "DATA: ",0
  
error_code:  
  .asciiz "ERROR CODE: $"
press_a_key_to_continue:
  .byte "PRESS A KEY TO CONTINUE",13,0

failed:
	.byte "FAILED ", 0

ok:
	.byte "OK ", 0
 
dns_lookup_failed_msg:
 .byte "DNS LOOKUP FAILED", 0

reply_message:
  .byte "PONG!"
reply_message_end:
reply_message_length=reply_message_end-reply_message
