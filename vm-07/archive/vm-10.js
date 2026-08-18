const PALETTE_RGBA = [
    [26, 28, 35], [56, 43, 61], [92, 53, 70], [143, 64, 77],
    [203, 77, 79], [250, 110, 89], [255, 155, 107], [255, 209, 117],
    [204, 246, 156], [121, 214, 125], [40, 164, 120], [29, 111, 120],
    [25, 60, 89], [18, 28, 46], [35, 41, 54], [107, 110, 128]
];

const MEM_SIZE = 65536;
const memory = new Uint8Array(MEM_SIZE);
const view16 = new DataView(memory.buffer); // Little-endian by default when 'true' flag is passed

const regs = new Uint16Array(16);
const RC = 0xC; // PC (12)
const RD = 0xD; // SP (13)
const RE = 0xE; // CSP (14)
const RF = 0xF; // FLAGS (15)

let isRunning = false;
let animationFrameId = null;
let frameCount = 0;
let activeKeyCode = 0;

const canvas = document.getElementById('screen');
const ctx = canvas.getContext('2d');
const imageData = ctx.createImageData(128, 128);
const dataBuffer = imageData.data;

// Memory Map Constants
const FB_START = 0x8000;         // Framebuffer start address
const FB_SIZE = 128 * 128;       // Framebuffer size (16,384 bytes)
const STACK_START = 0xF000;      // Stack start pointer
const CALL_STACK_START = 0xF400; // Call stack start pointer
const TIMESTAMP_ADDR = 0xFFF0;   // Timestamp MMIO (8 bytes)
const MOUSE_X_ADDR = 0xFFF8;     // Mouse X coordinate MMIO (1 byte)
const MOUSE_Y_ADDR = 0xFFF9;     // Mouse Y coordinate MMIO (1 byte)
const KEYBOARD_ADDR = 0xFFFC;    // Keyboard MMIO (1 byte)

const VALID_MNEMONICS = ['MOV', 'ADD', 'SUB', 'AND', 'SHL', 'CMP', 'JMP', 'JNZ', 'STR', 'LDR', 'VWAIT', 'HALT'];
const VALID_DIRECTIVES = ['.ORG', '.DW', '.DD', '.DB'];

function resetVM() {
    regs.fill(0);
    regs[RD] = STACK_START;
    regs[RE] = CALL_STACK_START;
    regs[RC] = 0x0100; // Program Counter reset address
    console.log("[Pantasya VM] VM Reset. Registers initialized:", Array.from(regs).map((r, i) => `R${i.toString(16).toUpperCase()}: 0x${r.toString(16)}`));
}

let instructionsTable = [];

// Helper function to parse numbers supporting 0x prefix and base suffixes (h/H, b/B, o/O, q/Q)
function parseNumber(str) {
    if (!str) throw new Error("Missing numerical value.");
    str = str.trim();
    let lower = str.toLowerCase();
    let val;
    
    if (lower.startsWith('0x')) {
        val = parseInt(str, 16);
    } else if (lower.endsWith('h')) {
        val = parseInt(str.slice(0, -1), 16);
    } else if (lower.endsWith('b')) {
        val = parseInt(str.slice(0, -1), 2);
    } else if (lower.endsWith('o') || lower.endsWith('q')) {
        val = parseInt(str.slice(0, -1), 8);
    } else {
        val = parseInt(str, 10);
    }
    
    if (isNaN(val)) {
        throw new Error(`Invalid number format: "${str}"`);
    }
    return val;
}

function parseAndLoad(source) {
    console.log("[Pantasya VM] Starting code assembly and loading...");
    instructionsTable = [];
    memory.fill(0);
    let lines = source.split('\n');
    let labels = {};
    let currentAddr = 0;
    let pendingLines = [];

    for (let lineNum = 0; lineNum < lines.length; lineNum++) {
        let rawLine = lines[lineNum];
        let line = rawLine.split(';')[0].trim();
        if (!line) continue;
        
        let parts = line.split(/\s+/);
        
        while (parts.length > 0 && parts[0].endsWith(':')) {
            let labelName = parts[0].slice(0, -1);
            if (!labelName) throw new Error(`Line ${lineNum + 1}: Empty label name.`);
            labels[labelName] = currentAddr;
            console.log(`[Assembler] Resolved Label "${labelName}" -> 0x${currentAddr.toString(16)}`);
            parts.shift();
        }
        if (parts.length === 0) continue;

        let mnemonicOrDir = parts[0];

        if (mnemonicOrDir.startsWith('.')) {
            let upperDir = mnemonicOrDir.toUpperCase();
            if (!VALID_DIRECTIVES.includes(upperDir)) {
                throw new Error(`Line ${lineNum + 1}: Unknown directive "${mnemonicOrDir}"`);
            }
            if (upperDir === '.ORG') {
                currentAddr = parseNumber(parts[1]);
                console.log(`[Assembler] Directive .ORG set currentAddr -> 0x${currentAddr.toString(16)}`);
            } else if (upperDir === '.DW') {
                let vals = parts.slice(1).join(' ').split(',').map(v => {
                    let valStr = v.trim();
                    if (valStr === '$') return currentAddr;
                    return parseNumber(valStr);
                });
                console.log(`[Assembler] Directive .DW at 0x${currentAddr.toString(16)} with values:`, vals);
                for (let val of vals) {
                    // Little-endian 16-bit write
                    view16.setUint16(currentAddr, val, true);
                    currentAddr += 2;
                }
            } else if (upperDir === '.DD') {
                let vals = parts.slice(1).join(' ').split(',').map(v => {
                    let valStr = v.trim();
                    if (valStr === '$') return currentAddr;
                    return parseNumber(valStr);
                });
                console.log(`[Assembler] Directive .DD at 0x${currentAddr.toString(16)} with values:`, vals);
                for (let val of vals) {
                    // Little-endian 32-bit write
                    view16.setUint32(currentAddr, val, true);
                    currentAddr += 4;
                }
            } else if (upperDir === '.DB') {
                let bytes = parts.slice(1).join(' ').split(',').map(b => {
                    let byteStr = b.trim();
                    if (byteStr === '$') return currentAddr & 0xFF;
                    return parseNumber(byteStr);
                });
                console.log(`[Assembler] Directive .DB at 0x${currentAddr.toString(16)} with bytes:`, bytes);
                for (let i = 0; i < bytes.length; i++) {
                    memory[currentAddr + i] = bytes[i] & 0xFF;
                }
                currentAddr += bytes.length;
            }
            continue;
        }

        if (!VALID_MNEMONICS.includes(mnemonicOrDir.toUpperCase())) {
            throw new Error(`Line ${lineNum + 1}: Unknown instruction mnemonic "${mnemonicOrDir}"`);
        }

        pendingLines.push({ addr: currentAddr, parts, lineNum: lineNum + 1 });
        currentAddr += 4;
    }

    for (let pl of pendingLines) {
        let parts = pl.parts;
        let mnemonic = parts[0].toUpperCase();
        let currentAddr = pl.addr; // capture instruction's address for `$`
        
        let getVal = (arg) => {
            if (!arg) return { type: 'IMM', val: 0 };
            arg = arg.replace(/,/g, '').trim();
            
            if (arg === '$') {
                return { type: 'IMM', val: currentAddr };
            }
            
            const regRegex = /^r[0-9a-f]$/i;
            if (regRegex.test(arg)) {
                let regIdx = parseInt(arg.slice(1), 16);
                return { type: 'REG', val: regIdx };
            }

            if (labels[arg] !== undefined) {
                return { type: 'IMM', val: labels[arg] };
            }
            return { type: 'IMM', val: parseNumber(arg) };
        };

        let arg1 = null;
        let arg2 = null;

        if (mnemonic === 'VWAIT' || mnemonic === 'HALT') {
            // 0 arguments
        } else if (mnemonic === 'JMP' || mnemonic === 'JNZ') {
            arg1 = getVal(parts[1]);
        } else {
            arg1 = getVal(parts[1]);
            arg2 = getVal(parts[2]);
            
            if (['MOV', 'ADD', 'SUB', 'AND', 'SHL', 'LDR'].includes(mnemonic) && arg1.type !== 'REG') {
                throw new Error(`Line ${pl.lineNum}: Instruction "${mnemonic}" expects a register destination as its first argument, but got "${parts[1]}"`);
            }
        }

        instructionsTable[pl.addr] = {
            mnemonic,
            arg1,
            arg2,
            lineNum: pl.lineNum // Store line number here
        };
        console.log(`[Assembler] Instruction [0x${pl.addr.toString(16)}] -> ${mnemonic}`, arg1, arg2);
    }
    console.log("[Pantasya VM] Assembly completed successfully.");
}

function assembleCode() {
    stopVM(); // Automatically stop program on assemble
    try {
        parseAndLoad(document.getElementById('source').value);
        console.log("[Pantasya VM] Assemble button triggered. Code is clean.");
        alert("Assembly successful! No errors found. Check console for detailed logs.");
    } catch (err) {
        console.error("[Assembler Error Halted]:", err.message);
        alert(`Assembly Error: ${err.message}`);
    }
}

function uploadFile(event) {
    const file = event.target.files[0];
    if (!file) return;
    stopVM(); // Automatically stop program on file upload
    const reader = new FileReader();
    reader.onload = function(e) {
        document.getElementById('source').value = e.target.result;
        console.log("[Pantasya VM] File uploaded successfully:", file.name);
    };
    reader.readAsText(file);
}

function stepVM() {
    let pc = regs[RC];
    let inst = instructionsTable[pc];
    if (!inst) {
        throw new Error(`Segmentation fault: Attempted to execute uninitialized instruction at PC = 0x${pc.toString(16)}`);
    }
    regs[RC] += 4;

    const op1 = inst.arg1;
    const op2 = inst.arg2;

    switch (inst.mnemonic) {
        case 'MOV':
            regs[op1.val] = op2.type === 'REG' ? regs[op2.val] : op2.val;
            break;
        case 'ADD':
            regs[op1.val] = (regs[op1.val] + (op2.type === 'REG' ? regs[op2.val] : op2.val)) & 0xFFFF;
            break;
        case 'SUB':
            regs[op1.val] = (regs[op1.val] - (op2.type === 'REG' ? regs[op2.val] : op2.val)) & 0xFFFF;
            break;
        case 'AND':
            regs[op1.val] = regs[op1.val] & (op2.type === 'REG' ? regs[op2.val] : op2.val);
            break;
        case 'SHL':
            regs[op1.val] = (regs[op1.val] << (op2.type === 'REG' ? regs[op2.val] : op2.val)) & 0xFFFF;
            break;
        case 'CMP': {
            let v1 = op1.type === 'REG' ? regs[op1.val] : op1.val;
            let v2 = op2.type === 'REG' ? regs[op2.val] : op2.val;
            regs[RF] = (v1 === v2 ? 1 : 0) | (v1 < v2 ? 2 : 0);
            break;
        }
        case 'JMP':
            regs[RC] = op1.type === 'REG' ? regs[op1.val] : op1.val;
            break;
        case 'JNZ':
            if ((regs[RF] & 1) === 0) {
                regs[RC] = op1.type === 'REG' ? regs[op1.val] : op1.val;
            }
            break;
        case 'STR': {
            let addr = op1.type === 'REG' ? regs[op1.val] : op1.val;
            let val = op2.type === 'REG' ? regs[op2.val] : op2.val;
            if (addr >= MEM_SIZE || (addr < FB_START && addr + 1 >= MEM_SIZE)) {
                throw new Error(`Memory Access Error: Out of bounds write at address 0x${addr.toString(16)}`);
            }
            if (addr >= FB_START && addr < FB_START + FB_SIZE) {
                memory[addr] = val & 0xFF;
            } else {
                // Little-endian 16-bit store
                view16.setUint16(addr, val, true);
            }
            break;
        }
        case 'LDR': {
            let addr = op2.type === 'REG' ? regs[op2.val] : op2.val;
            if (addr + 1 >= MEM_SIZE) {
                throw new Error(`Memory Access Error: Out of bounds read at address 0x${addr.toString(16)}`);
            }
            // Little-endian 16-bit load
            regs[op1.val] = view16.getUint16(addr, true);
            break;
        }
        case 'VWAIT':
            return false;
        case 'HALT':
            stopVM();
            return false;
    }
    return true;
}

function updatePeripherals() {
    // Sync system timestamp dynamically into MMIO memory map as a 64-bit little-endian value
    view16.setBigUint64(TIMESTAMP_ADDR, BigInt(Date.now()), true);
}

function renderScreen() {
    let fb = memory.subarray(FB_START, FB_START + FB_SIZE);

    for (let i = 0; i < FB_SIZE; i++) {
        let val = fb[i] & 0x0F;
        let rgb = PALETTE_RGBA[val];
        let px = i * 4;
        dataBuffer[px] = rgb[0];
        dataBuffer[px + 1] = rgb[1];
        dataBuffer[px + 2] = rgb[2];
        dataBuffer[px + 3] = 255;
    }
    ctx.putImageData(imageData, 0, 0);
}

function frame() {
    try {
        frameCount++;
        for (let i = 0; i < 200000; i++) {
            let running = stepVM();
            if (!running) break;
        }
        updatePeripherals();
        renderScreen();

        if (frameCount % 60 === 0) {
            console.log(`[Pantasya VM] Frame ${frameCount} rendered. PC: 0x${regs[RC].toString(16)}, SP: 0x${regs[RD].toString(16)}`);
        }

        if (isRunning) {
            animationFrameId = requestAnimationFrame(frame);
        }
    } catch(err) {
        let failingInst = instructionsTable[regs[RC] >= 4 ? regs[RC] - 4 : regs[RC]];
        let lineInfo = failingInst && failingInst.lineNum ? ` (Source Line: ${failingInst.lineNum})` : "";
        
        console.error(`[Runtime Error Halting VM]${lineInfo}:`, err.message);
        alert(`Runtime Error${lineInfo}: ${err.message}`);
        stopVM();
    }
}

function toggleRun() {
    if (isRunning) {
        stopVM();
    } else {
        startVM();
    }
}

function startVM() {
    try {
        console.log("[Pantasya VM] Starting execution...");
        parseAndLoad(document.getElementById('source').value);
        resetVM();
        isRunning = true;
        document.getElementById('runBtn').innerText = 'STOP';
        document.getElementById('runBtn').classList.add('stop');
        
        updatePeripherals();
        renderScreen();

        animationFrameId = requestAnimationFrame(frame);
    } catch (err) {
        console.error("[Assembly Error Halting Execution]:", err.message);
        alert(`Assembly Error: ${err.message}`);
        stopVM();
    }
}

function stopVM() {
    isRunning = false;
    if (animationFrameId) cancelAnimationFrame(animationFrameId);
    document.getElementById('runBtn').innerText = 'RUN';
    document.getElementById('runBtn').classList.remove('stop');
    console.log("[Pantasya VM] Execution stopped by user or error.");
}

// Grab mouse on canvas click using Pointer Lock API
canvas.addEventListener('click', () => {
    canvas.requestPointerLock();
});

// Handle mouse movement (supports both pointer lock relative movement and normal absolute hover)
document.addEventListener('mousemove', (e) => {
    if (document.pointerLockElement === canvas) {
        let x = memory[MOUSE_X_ADDR] + e.movementX;
        let y = memory[MOUSE_Y_ADDR] + e.movementY;
        x = Math.max(0, Math.min(127, x));
        y = Math.max(0, Math.min(127, y));
        memory[MOUSE_X_ADDR] = x & 0xFF;
        memory[MOUSE_Y_ADDR] = y & 0xFF;
    } else {
        const rect = canvas.getBoundingClientRect();
        const scaleX = canvas.width / rect.width;
        const scaleY = canvas.height / rect.height;
        const x = Math.floor((e.clientX - rect.left) * scaleX);
        const y = Math.floor((e.clientY - rect.top) * scaleY);
        
        if (x >= 0 && x < 128 && y >= 0 && y < 128) {
            memory[MOUSE_X_ADDR] = x & 0xFF;
            memory[MOUSE_Y_ADDR] = y & 0xFF;
        }
    }
});

// Textarea shortcut handler for Ctrl+L (Go to Line)
const sourceTextarea = document.getElementById('source');
if (sourceTextarea) {
    sourceTextarea.addEventListener('keydown', (e) => {
        if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'l') {
            e.preventDefault(); // Prevent browser address bar focus or other defaults
            let lines = sourceTextarea.value.split('\n');
            let input = prompt(`Go to line (1 - ${lines.length}):`);
            if (input !== null) {
                let targetLine = parseInt(input, 10);
                if (!isNaN(targetLine) && targetLine >= 1 && targetLine <= lines.length) {
                    let charIndex = 0;
                    for (let i = 0; i < targetLine - 1; i++) {
                        charIndex += lines[i].length + 1; // +1 for the newline character
                    }
                    sourceTextarea.focus();
                    sourceTextarea.setSelectionRange(charIndex, charIndex);
                    
                    // Scroll textarea to make the line visible
                    let lineHeight = sourceTextarea.scrollHeight / lines.length;
                    sourceTextarea.scrollTop = (targetLine - 1) * lineHeight;
                } else {
                    alert("Invalid line number.");
                }
            }
        }
    });
}

window.addEventListener('keydown', (e) => {
    if (e.target.id === 'source') return;

    if (e.key === 'Escape' || e.keyCode === 27) {
        if (document.pointerLockElement === canvas) {
            document.exitPointerLock();
            return;
        }
    }

    let code = e.keyCode;
    if (e.key === 'ArrowLeft' || e.code === 'ArrowLeft') code = 37;
    else if (e.key === 'ArrowRight' || e.code === 'ArrowRight') code = 39;
    else if (e.key === 'ArrowUp' || e.code === 'ArrowUp') code = 38;
    else if (e.key === 'ArrowDown' || e.code === 'ArrowDown') code = 40;
    else if (e.key === 'Enter' || e.code === 'Enter') code = 13;

    if ([37, 39, 38, 40].includes(code)) e.preventDefault();
    activeKeyCode = code & 0xFF;
    memory[KEYBOARD_ADDR] = activeKeyCode;
    console.log(`[Peripherals] Key Down: key = ${e.key}, code = ${code}`);
});

window.addEventListener('keyup', (e) => {
    if (e.target.id === 'source') return;

    let code = e.keyCode;
    if (e.key === 'ArrowLeft' || e.code === 'ArrowLeft') code = 37;
    else if (e.key === 'ArrowRight' || e.code === 'ArrowRight') code = 39;
    else if (e.key === 'ArrowUp' || e.code === 'ArrowUp') code = 38;
    else if (e.key === 'ArrowDown' || e.code === 'ArrowDown') code = 40;
    else if (e.key === 'Enter' || e.code === 'Enter') code = 13;

    if (code === activeKeyCode) {
        memory[KEYBOARD_ADDR] = 0;
        activeKeyCode = 0;
    }
});