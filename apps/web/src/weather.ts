/** Quiet, compact topbar weather; refresh never changes the surrounding layout. */
export function mountWeather(root: HTMLElement): () => void {
  root.innerHTML = `<span class="weather-icon" aria-hidden="true">☀</span><span class="weather-city" role="button" tabindex="0" title="点击重新定位；授权后向 OpenStreetMap 发送城市级粗略坐标 · © OpenStreetMap contributors">北京</span><span class="weather-condition">—</span><span class="weather-temperature">—°C</span>`;
  const city = root.querySelector<HTMLElement>('.weather-city')!;
  const condition = root.querySelector<HTMLElement>('.weather-condition')!;
  const temperature = root.querySelector<HTMLElement>('.weather-temperature')!;
  const icon = root.querySelector<HTMLElement>('.weather-icon')!;
  let cityName = '北京';
  let locationController: AbortController | undefined;
  let locating = false;
  let busy = false;
  let disposed = false;
  let controller: AbortController | undefined;
  async function refresh() {
    if (busy || disposed || document.hidden) return;
    busy = true;
    const requestController = new AbortController();
    controller = requestController;
    const timeout = window.setTimeout(() => requestController.abort(), 28000);
    const requestedCity = cityName;
    try {
      const response = await fetch(`/weather?city=${encodeURIComponent(requestedCity)}`, { signal: requestController.signal });
      const data = await response.json() as { success: boolean; text?: string };
      if (!response.ok || !data.success || !data.text || disposed || requestedCity !== cityName) return;
      const field = (name: string) => data.text!.match(new RegExp(`^${name}:\\s*(.+)$`, 'm'))?.[1]?.trim();
      const name = field('城市')?.split(',')[0];
      const weather = field('天气')?.replace(/\s*\(\d+\)$/, '');
      const degrees = field('温度')?.replace(/\s+/g, '');
      if (!name || !weather || !degrees) return;
      city.textContent = name;
      condition.textContent = weather;
      temperature.textContent = degrees;
      icon.textContent = /雷/.test(weather) ? '⛈' : /雪/.test(weather) ? '❄' : /雨|毛毛/.test(weather) ? '☂' : /雾/.test(weather) ? '≋' : /阴|云/.test(weather) ? '☁' : '☀';
    } catch {
      // Keep the last successful reading without adding refresh/error logs.
    } finally {
      window.clearTimeout(timeout);
      busy = false;
      if (!disposed && requestedCity !== cityName) void refresh();
    }
  }
  function locate() {
    if (disposed || locating || typeof navigator === 'undefined' || !navigator.geolocation) return;
    locating = true;
    navigator.geolocation.getCurrentPosition(async (position) => {
      if (disposed) { locating = false; return; }
      // Send city-scale coordinates only, not precise location.
      const lat = (Math.round(position.coords.latitude * 10) / 10).toFixed(1);
      const lon = (Math.round(position.coords.longitude * 10) / 10).toFixed(1);
      locationController = new AbortController();
      const currentController = locationController;
      const timeout = window.setTimeout(() => currentController.abort(), 12000);
      try {
        const response = await fetch(`/location?lat=${lat}&lon=${lon}`, { signal: currentController.signal });
        const data = await response.json() as { success: boolean; city?: string };
        if (response.ok && data.success && data.city && !disposed) {
          cityName = data.city;
          city.textContent = cityName;
          city.title = '点击重新定位 · 城市数据 © OpenStreetMap contributors';
          void refresh();
        }
      } catch { /* Keep the fallback or previous city quietly. */ }
      finally { window.clearTimeout(timeout); locating = false; }
    }, () => { locating = false; }, { enableHighAccuracy: false, timeout: 10000, maximumAge: 3600000 });
  }
  function locationKey(event: KeyboardEvent) {
    if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); locate(); }
  }
  city.addEventListener('click', locate);
  city.addEventListener('keydown', locationKey);
  locate();
  function visibility() { if (!document.hidden) void refresh(); }
  document.addEventListener('visibilitychange', visibility);
  const interval = window.setInterval(() => void refresh(), 10000);
  void refresh();
  return () => {
    disposed = true;
    window.clearInterval(interval);
    controller?.abort();
    locationController?.abort();
    city.removeEventListener('click', locate);
    city.removeEventListener('keydown', locationKey);
    document.removeEventListener('visibilitychange', visibility);
  };
}
