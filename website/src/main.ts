const FRAMES: string[][] = [
  ["*", " ", " ", " ", " ", " "], // [*     ]
  ["+", "*", " ", " ", " ", " "], // [**    ]
  ["*", "+", "*", " ", " ", " "], // [***   ]
  [" ", "*", "+", "*", " ", " "], // [ ***  ]
  [" ", " ", "*", "+", "*", " "], // [  *** ]
  [" ", " ", " ", "*", "+", "*"], // [   ***]
  [" ", " ", " ", " ", "*", "+"], // [    **]
  [" ", " ", " ", " ", " ", "*"], // [     *]
  [" ", " ", " ", " ", "*", "+"], // [    **]
  [" ", " ", " ", "*", "+", "*"], // [   ***]
  [" ", " ", "*", "+", "*", " "], // [  *** ]
  [" ", "*", "+", "*", " ", " "], // [ ***  ]
  ["*", "+", "*", " ", " ", " "], // [***   ]
  ["+", "*", " ", " ", " ", " "], // [**    ]
];

const cells = document.querySelectorAll<HTMLElement>("#spinner .sc");

if (cells.length === FRAMES[0].length) {
  let fi = 0;
  const tick = () => {
    const frame = FRAMES[fi % FRAMES.length];
    cells.forEach((cell, i) => {
      const state = frame[i];
      const lit = state !== " ";
      cell.className = lit ? "sc sc-on" : "sc sc-off";
      cell.style.opacity = lit ? (state === "+" ? "1" : "0.45") : "";
      cell.textContent = lit ? "*" : " ";
    });
    fi++;
  };
  tick();
  setInterval(tick, 900);
}
