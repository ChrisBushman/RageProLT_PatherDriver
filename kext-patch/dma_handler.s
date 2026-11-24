.section __TEXT,__text,regular,pure_instructions
	.align 2
	.align 2
.section __TEXT,__text,regular,pure_instructions
	.align 2
_gui_idle:
	lis r11,0x1e
	li r9,0
	ori r11,r11,33919
	li r2,824
L9:
	lwbrx r0,r3,r2
	andi. r10,r0,1
	bne- cr0,L4
	lwbrx r0,r3,r2
	andi. r10,r0,1
	li r0,1
	beq- cr0,L1
L4:
	addi r9,r9,1
	cmplw cr7,r9,r11
	ble+ cr7,L9
	li r0,0
L1:
	mr r3,r0
	blr
	.align 2
	.globl _dma_handler
.section __TEXT,__text,regular,pure_instructions
	.align 2
_dma_handler:
	stw r30,-8(r1)
	mr. r30,r4
	mflr r0
	stw r23,-36(r1)
	stw r24,-32(r1)
	mr r23,r7
	stw r25,-28(r1)
	mr r24,r5
	stw r27,-20(r1)
	mr r25,r8
	stw r29,-12(r1)
	li r27,0
	stw r21,-44(r1)
	mr r29,r6
	stw r22,-40(r1)
	stw r26,-24(r1)
	stw r28,-16(r1)
	stw r31,-4(r1)
	stw r0,8(r1)
	stwu r1,-128(r1)
	beq- cr0,L17
	cmplwi cr7,r6,7
	ble- cr7,L17
	lwz r3,176(r3)
	lis r0,0xe000
	ori r0,r0,706
	lwz r31,0(r30)
	cmplwi cr7,r3,65535
	lwz r22,4(r30)
	ble- cr7,L12
	lwz r26,1348(r3)
	lwz r28,1360(r3)
	cmpwi cr7,r26,0
	beq- cr7,L17
	cmplwi cr7,r28,65535
	bgt- cr7,L16
L17:
	lis r0,0xe000
	ori r0,r0,706
	b L12
L16:
	mr r3,r26
	bl _mutex_lock
	cmpwi cr7,r31,7
	bne+ cr7,L18
	xoris r0,r22,0x5a5a
	xori r0,r0,4660
	stw r0,28(r25)
	b L20
L18:
	cmpwi cr7,r31,0
	bne- cr7,L21
	stw r27,12(r25)
	b L20
L21:
	cmpwi cr7,r31,1
	bne+ cr7,L23
	lwz r0,0(r25)
	lis r31,0x444d
	ori r31,r31,16689
	addi r29,r29,-8
	cmpw cr7,r0,r31
	beq- cr7,L24
	lis r3,0x2
	li r4,16384
	ori r3,r3,16384
	addi r5,r1,64
	stw r27,64(r1)
	bl _IOMallocContiguous
	mr. r3,r3
	beq- cr0,L26
	lwz r9,64(r1)
	cmpwi cr7,r9,0
	beq- cr7,L26
	andi. r0,r3,16383
	bne- cr0,L26
	andi. r2,r9,16383
	bne- cr0,L26
	addis r2,r9,0x2
	lis r0,0x8000
	addi r2,r2,16384
	cmplw cr7,r2,r0
	ble- cr7,L25
L26:
	lis r27,0xe000
	ori r27,r27,701
	b L24
L25:
	stw r3,4(r25)
	stw r9,8(r25)
	stw r31,0(r25)
	stw r27,12(r25)
L24:
	cmpwi cr6,r27,0
	bne- cr6,L20
	cmpwi cr7,r29,0
	beq- cr7,L29
	andi. r4,r29,3
	bne- cr0,L29
	lwz r0,12(r25)
	lis r2,0x1
	ori r2,r2,65528
	add r0,r0,r29
	cmplw cr7,r0,r2
	ble- cr7,L28
L29:
	lis r27,0xe000
	ori r27,r27,706
	cmpwi cr6,r27,0
L28:
	bne- cr6,L20
	srwi r9,r29,2
	lwz r2,4(r25)
	cmplw cr7,r27,r9
	lwz r0,12(r25)
	add r2,r2,r0
	addi r2,r2,16384
	bge- cr7,L117
	mtctr r9
	addi r4,r30,8
L136:
	lwz r0,0(r4)
	addi r4,r4,4
	stwbrx r0,0,r2
	addi r2,r2,4
	bdnz L136
	lwz r0,12(r25)
L117:
	add r0,r0,r29
	b L138
L23:
	cmpwi cr7,r31,2
	bne+ cr7,L38
	lwz r0,0(r25)
	lis r2,0x444d
	ori r2,r2,16689
	lwz r8,12(r25)
	cmpw cr7,r0,r2
	bne+ cr7,L40
	cmpwi cr7,r8,0
	bne- cr7,L39
L40:
	lis r27,0xe000
	ori r27,r27,706
L39:
	cmpwi cr6,r27,0
	lwz r5,4(r25)
	lwz r29,8(r25)
	addi r4,r5,16384
	addi r21,r29,16384
	bne- cr6,L59
	cmplw cr7,r27,r8
	li r7,0
	bge- cr7,L41
L58:
	add r0,r4,r7
	lwbrx r0,0,r0
	andi. r2,r0,65024
	srwi r2,r0,16
	rlwinm r10,r0,0,16,31
	addi r11,r2,1
	bne- cr0,L47
	cmplwi cr7,r2,1023
	bgt- cr7,L47
	slwi r6,r11,2
	add r2,r7,r6
	addi r2,r2,4
	cmplw cr7,r2,r8
	ble+ cr7,L46
L47:
	lis r27,0xe000
	ori r27,r27,706
	cmpwi cr6,r27,0
	b L41
L46:
	li r9,0
	cmplw cr7,r9,r11
	bge- cr7,L49
L56:
	add r2,r10,r9
	cmpwi cr7,r2,109
	beq- cr7,L130
	cmpwi cr7,r2,40
	beq- cr7,L130
	cmpwi cr7,r2,146
	beq- cr7,L130
	cmpwi cr7,r2,147
	beq- cr7,L130
	cmpwi cr7,r2,366
	beq- cr7,L130
	cmpwi cr7,r2,367
	beq- cr7,L130
	addi r0,r2,-352
	cmplwi cr7,r0,3
	bgt+ cr7,L50
L130:
	lis r27,0xe000
	ori r27,r27,706
	cmpwi cr6,r27,0
	b L49
L50:
	addi r9,r9,1
	cmplw cr7,r9,r11
	blt+ cr7,L56
L49:
	bne- cr6,L59
	add r2,r7,r6
	addi r7,r2,4
	cmplw cr7,r7,r8
	blt+ cr7,L58
L41:
	bne- cr6,L59
	add r2,r4,r8
	li r0,109
	stwbrx r0,0,r2
	li r0,0
	addi r2,r2,4
	stwbrx r0,0,r2
	lis r0,0x2008
	addi r9,r8,4099
	ori r0,r0,513
	srwi r9,r9,2
	addi r31,r8,8
	mulhwu r9,r9,r0
	li r7,0
	srwi r12,r9,7
	cmplw cr7,r27,r12
	bge- cr7,L121
	lis r6,0x7f
	mtctr r12
	ori r6,r6,65096
	li r3,0
	li r8,0
L135:
	subf r10,r8,r31
	cmplwi cr7,r10,4092
	ble- cr7,L66
	li r10,4092
L66:
	slwi r0,r7,4
	add r11,r5,r0
	stwbrx r6,0,r11
	addi r9,r11,4
	add r0,r21,r8
	stwbrx r0,0,r9
	add r2,r8,r10
	cmplw cr7,r2,r31
	oris r0,r10,0x4000
	addi r2,r11,8
	blt- cr7,L69
	oris r0,r10,0xc000
L69:
	stwbrx r0,0,r2
	addi r0,r11,12
	stwbrx r3,0,r0
	addi r7,r7,1
	addi r8,r8,4092
	bdnz L135
L121:
	slwi r0,r12,4
	rlwinm r9,r5,0,0,26
	add r2,r5,r0
	cmplw cr7,r9,r2
	bge- cr7,L123
	subf r2,r9,r2
	addi r2,r2,31
	srwi r2,r2,5
	mtctr r2
L134:
	dcbf 0,r9
	addi r9,r9,32
	bdnz L134
L123:
	sync
	add r2,r4,r31
	rlwinm r9,r4,0,0,26
	cmplw cr7,r9,r2
	bge- cr7,L125
	subf r2,r9,r2
	addi r2,r2,31
	srwi r2,r2,5
	mtctr r2
L133:
	dcbf 0,r9
	addi r9,r9,32
	bdnz L133
L125:
	sync
	cmpwi cr7,r24,0
	add r30,r29,r0
	beq- cr7,L85
	cmpwi cr7,r23,0
	beq- cr7,L85
	lwz r0,0(r23)
	cmplwi cr7,r0,15
	ble- cr7,L85
	li r0,16
	stw r21,12(r24)
	stw r12,0(r24)
	stw r31,4(r24)
	stw r29,8(r24)
	stw r0,0(r23)
L85:
	andi. r4,r22,1
	bne+ cr0,L59
	mr r3,r28
	bl _gui_idle
	li r5,0
	cmpwi cr7,r3,0
	bne- cr7,L87
	lis r27,0xe000
	b L137
L131:
	li r5,1
	b L103
L87:
	li r9,160
	lwbrx r8,r28,r9
	li r11,436
	lwbrx r7,r28,r11
	li r2,-584
	lwbrx r6,r28,r2
	rlwinm r0,r8,0,26,24
	oris r0,r0,0x800
	stwbrx r0,r28,r9; eieio
	li r2,588
	stwbrx r29,r28,r2; eieio
	li r0,3840
	stwbrx r0,r28,r11; eieio
	sync
	li r2,280
	li r0,0
	stwbrx r0,r28,r2; eieio
	li r0,8
	mtctr r0
	li r2,824
L132:
	lwbrx r0,r28,r2
	bdnz L132
	lis r11,0x3d
	li r2,0
	ori r11,r11,2303
	li r9,-584
	li r10,824
L109:
	lwbrx r0,r28,r9
	cmpw cr7,r0,r30
	bne+ cr7,L104
	lwbrx r0,r28,r10
	andi. r4,r0,1
	beq- cr0,L131
L104:
	addi r2,r2,1
	cmplw cr7,r2,r11
	ble+ cr7,L109
L103:
	li r0,436
	stwbrx r7,r28,r0; eieio
	li r0,588
	rlwinm r9,r6,0,0,27
	stwbrx r9,r28,r0; eieio
	li r2,160
	stwbrx r8,r28,r2; eieio
	cmpwi cr7,r5,0
	beq- cr7,L113
	lwz r2,16(r25)
	lwz r0,20(r25)
	addi r2,r2,1
	add r0,r0,r31
	stw r2,16(r25)
	stw r0,20(r25)
	b L59
L113:
	lwz r2,24(r25)
	lis r27,0xe000
	addi r2,r2,1
	stw r2,24(r25)
L137:
	ori r27,r27,726
L59:
	li r0,0
L138:
	stw r0,12(r25)
	b L20
L38:
	lis r27,0xe000
	ori r27,r27,706
L20:
	mr r3,r26
	bl _mutex_unlock
	mr r0,r27
L12:
	mr r3,r0
	lwz r0,136(r1)
	addi r1,r1,128
	mtlr r0
	lwz r21,-44(r1)
	lwz r22,-40(r1)
	lwz r23,-36(r1)
	lwz r24,-32(r1)
	lwz r25,-28(r1)
	lwz r26,-24(r1)
	lwz r27,-20(r1)
	lwz r28,-16(r1)
	lwz r29,-12(r1)
	lwz r30,-8(r1)
	lwz r31,-4(r1)
	blr
