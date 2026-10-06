// Persistent helper embedded in the plugin, then dlopen'd with RTLD_NODELETE.
.section .rodata
.global applestiaShapesHelperBlobStart
.balign 8
applestiaShapesHelperBlobStart:
.incbin "src/shapes-helper/applestia-shapes-helper.so"
.global applestiaShapesHelperBlobEnd
applestiaShapesHelperBlobEnd:
.section .note.GNU-stack,"",%progbits
