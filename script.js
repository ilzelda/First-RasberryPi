let bgGray = 255;

// 센서 값 가져와서 배경 어둡게
async function updateBackground() {
  try {
    const res = await fetch('/sensor');
    if (!res.ok) return;
    const value = parseInt((await res.text()).trim(), 10);
    if (value > 220) {
      bgGray = Math.max(0, bgGray - 20);
    }
  } catch (err) {
    console.error(err);
  }
}

setInterval(updateBackground, 500);

window.addEventListener('load', () => {
  const canvas = document.getElementById('gameCanvas');
  const ctx = canvas.getContext('2d');

  const player = { x: 50, y: 50, size: 32, speed: 3 };
  const keyItem = { x: 300, y: 200, size: 24, collected: false };
  const door = { x: 520, y: 320, width: 48, height: 64 };
  let hasKey = false;
  let gameCleared = false;

  // 배경 밝기 값을 0~255 범위로 관리

  const keys = {};

  window.addEventListener('keydown', e => {
    keys[e.code] = true;
    if (e.code === 'Space') tryCollectKey();
  });
  window.addEventListener('keyup', e => {
    keys[e.code] = false;
  });

  function tryCollectKey() {
    if (!keyItem.collected && isColliding(player, keyItem)) {
      keyItem.collected = true;
      hasKey = true;

      // 부저 울리기
      fetch('/buzzer')
        .then(res => {
          if (!res.ok) throw new Error('buzzer request failed');
        })
        .catch(console.error);
    }
  }

  function tryClear() {
    if (hasKey && rectIntersect(player, door)) {
      gameCleared = true;
    }
  }

  function isColliding(a, b) {
    return !(
      a.x + a.size < b.x ||
      a.x > b.x + b.size ||
      a.y + a.size < b.y ||
      a.y > b.y + b.size
    );
  }

  function rectIntersect(a, b) {
    return !(
      a.x + a.size < b.x ||
      a.x > b.x + b.width ||
      a.y + a.size < b.y ||
      a.y > b.y + b.height
    );
  }

  function update() {
    if (gameCleared) return;

    if (keys['ArrowUp'])    player.y -= player.speed;
    if (keys['ArrowDown'])  player.y += player.speed;
    if (keys['ArrowLeft'])  player.x -= player.speed;
    if (keys['ArrowRight']) player.x += player.speed;

    player.x = Math.max(0, Math.min(canvas.width - player.size, player.x));
    player.y = Math.max(0, Math.min(canvas.height - player.size, player.y));

    tryClear();
  }

  function draw() {
    // 배경색 칠하기 (bgGray 사용)
    ctx.fillStyle = `rgb(${bgGray}, ${bgGray}, ${bgGray})`;
    ctx.fillRect(0, 0, canvas.width, canvas.height);

    // 문
    ctx.fillStyle = hasKey ? '#8b4513' : '#654321';
    ctx.fillRect(door.x, door.y, door.width, door.height);

    // 열쇠
    if (!keyItem.collected) {
      ctx.fillStyle = 'white';
      ctx.fillRect(keyItem.x, keyItem.y, keyItem.size, keyItem.size);
    }

    // 플레이어
    ctx.fillStyle = 'steelblue';
    ctx.fillRect(player.x, player.y, player.size, player.size);

    // 클리어 메시지
    if (gameCleared) {
      ctx.fillStyle = 'rgba(0, 0, 0, 0.7)';
      ctx.fillRect(0, 0, canvas.width, canvas.height);
      ctx.fillStyle = '#fff';
      ctx.font = '48px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText('🎉 CLEAR! 🎉', canvas.width / 2, canvas.height / 2);
    }
  }

  function loop() {
    update();
    draw();
    requestAnimationFrame(loop);
  }

  loop();


});
