import React, { useEffect, useRef } from 'react';

interface RpmGaugeProps {
  rpm: number;
  maxRpm?: number;
}

export const RpmGauge: React.FC<RpmGaugeProps> = ({ rpm, maxRpm = 6000 }) => {
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

    const startAngle = Math.PI * 0.75;
    const endAngle = Math.PI * 2.25;

    // Track
    ctx.beginPath();
    ctx.arc(centerX, centerY, radius, startAngle, endAngle);
    ctx.lineWidth = 12;
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.08)';
    ctx.stroke();

    // Active RPM Arc
    const rpmRatio = Math.min(Math.max(rpm, 0), maxRpm) / maxRpm;
    const rpmAngle = startAngle + (endAngle - startAngle) * rpmRatio;

    ctx.beginPath();
    ctx.arc(centerX, centerY, radius, startAngle, rpmAngle);
    ctx.lineWidth = 12;
    ctx.strokeStyle = rpm > maxRpm * 0.8 ? '#ff0055' : '#4facfe';
    ctx.lineCap = 'round';
    ctx.stroke();

    // Center Display
    ctx.font = '700 36px "Outfit", sans-serif';
    ctx.fillStyle = '#ffffff';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(Math.round(rpm).toString(), centerX, centerY - 10);

    ctx.font = '600 13px "Outfit", sans-serif';
    ctx.fillStyle = 'rgba(255, 255, 255, 0.5)';
    ctx.fillText('RPM', centerX, centerY + 25);
  }, [rpm, maxRpm]);

  return (
    <div style={{ textAlign: 'center' }}>
      <canvas ref={canvasRef} width={220} height={220} />
    </div>
  );
};
