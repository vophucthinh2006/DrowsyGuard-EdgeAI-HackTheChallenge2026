import React, { useEffect, useRef } from 'react';

interface SpeedometerProps {
  speedKmh: number;
  speedCapPct: number;
  maxSpeed?: number;
}

export const Speedometer: React.FC<SpeedometerProps> = ({
  speedKmh,
  speedCapPct,
  maxSpeed = 120,
}) => {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const width = canvas.width;
    const height = canvas.height;
    const centerX = width / 2;
    const centerY = height / 2 + 10;
    const radius = Math.min(width, height) / 2 - 20;

    ctx.clearRect(0, 0, width, height);

    // Angles: -225 deg to 45 deg (270 degree sweep)
    const startAngle = Math.PI * 0.75;
    const endAngle = Math.PI * 2.25;

    // Outer Track
    ctx.beginPath();
    ctx.arc(centerX, centerY, radius, startAngle, endAngle);
    ctx.lineWidth = 14;
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.08)';
    ctx.stroke();

    // Speed Cap Limit Arc
    const capFraction = speedCapPct / 100;
    const capAngle = startAngle + (endAngle - startAngle) * capFraction;

    if (speedCapPct < 100) {
      ctx.beginPath();
      ctx.arc(centerX, centerY, radius, capAngle, endAngle);
      ctx.lineWidth = 14;
      ctx.strokeStyle = 'rgba(255, 0, 85, 0.25)';
      ctx.stroke();
    }

    // Active Speed Value Arc
    const speedRatio = Math.min(Math.max(speedKmh, 0), maxSpeed) / maxSpeed;
    const speedAngle = startAngle + (endAngle - startAngle) * speedRatio;

    const gradient = ctx.createLinearGradient(0, 0, width, 0);
    gradient.addColorStop(0, '#00f2fe');
    gradient.addColorStop(1, speedCapPct < 100 ? '#ff0055' : '#00ff88');

    ctx.beginPath();
    ctx.arc(centerX, centerY, radius, startAngle, speedAngle);
    ctx.lineWidth = 14;
    ctx.strokeStyle = gradient;
    ctx.lineCap = 'round';
    ctx.shadowColor = speedCapPct < 100 ? '#ff0055' : '#00f2fe';
    ctx.shadowBlur = 15;
    ctx.stroke();
    ctx.shadowBlur = 0; // Reset shadow

    // Ticks & Labels
    const totalTicks = 12;
    for (let i = 0; i <= totalTicks; i++) {
      const angle = startAngle + (endAngle - startAngle) * (i / totalTicks);
      const isMajor = i % 2 === 0;
      const tickLength = isMajor ? 12 : 6;

      const x1 = centerX + Math.cos(angle) * (radius - 18);
      const y1 = centerY + Math.sin(angle) * (radius - 18);
      const x2 = centerX + Math.cos(angle) * (radius - 18 - tickLength);
      const y2 = centerY + Math.sin(angle) * (radius - 18 - tickLength);

      ctx.beginPath();
      ctx.moveTo(x1, y1);
      ctx.lineTo(x2, y2);
      ctx.lineWidth = isMajor ? 3 : 1.5;
      ctx.strokeStyle = isMajor ? 'rgba(255, 255, 255, 0.5)' : 'rgba(255, 255, 255, 0.2)';
      ctx.stroke();

      if (isMajor) {
        const val = Math.round((i / totalTicks) * maxSpeed);
        const lx = centerX + Math.cos(angle) * (radius - 42);
        const ly = centerY + Math.sin(angle) * (radius - 42);

        ctx.font = '600 12px "Outfit", sans-serif';
        ctx.fillStyle = 'rgba(255, 255, 255, 0.6)';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(val.toString(), lx, ly);
      }
    }

    // Needle
    ctx.beginPath();
    ctx.moveTo(centerX, centerY);
    const nx = centerX + Math.cos(speedAngle) * (radius - 24);
    const ny = centerY + Math.sin(speedAngle) * (radius - 24);
    ctx.lineTo(nx, ny);
    ctx.lineWidth = 4;
    ctx.strokeStyle = '#ffffff';
    ctx.lineCap = 'round';
    ctx.stroke();

    // Pivot Circle
    ctx.beginPath();
    ctx.arc(centerX, centerY, 8, 0, Math.PI * 2);
    ctx.fillStyle = '#ffffff';
    ctx.fill();

    // Center Display Speed Text
    ctx.font = '800 48px "Outfit", sans-serif';
    ctx.fillStyle = '#ffffff';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(Math.round(speedKmh).toString(), centerX, centerY - 15);

    ctx.font = '600 14px "Outfit", sans-serif';
    ctx.fillStyle = 'rgba(255, 255, 255, 0.5)';
    ctx.fillText('KM/H', centerX, centerY + 25);

    if (speedCapPct < 100) {
      ctx.font = '700 11px "Outfit", sans-serif';
      ctx.fillStyle = '#ff0055';
      ctx.fillText(`CAP: ${speedCapPct}%`, centerX, centerY + 45);
    }
  }, [speedKmh, speedCapPct, maxSpeed]);

  return (
    <div style={{ textAlign: 'center' }}>
      <canvas ref={canvasRef} width={260} height={260} />
    </div>
  );
};
