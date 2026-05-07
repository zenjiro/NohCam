import numpy as np
import cv2
import json
from PIL import Image, ImageDraw, ImageFont
from OpenGL.GL import *
from typing import Optional, Dict, List


class ParameterDisplayRenderer:
    """Renders Live2D parameter values with bar graphs in the top-right corner."""

    def __init__(self, width: int = 1280, height: int = 720, margin: int = 100):
        self.width = width
        self.height = height
        self.margin = margin
        self.texture_id = None
        self.texture_data = None
        
        # Parameter info: {param_index: {"name": str, "min": float, "max": float}}
        self.param_info: Dict[int, Dict] = {}
        self.param_values: Dict[int, float] = {}
        
        # Display settings
        self.font_size = 12
        self.bar_length = 20
        self.line_height = 18
        self.panel_width = 500
        self.bg_color = (0, 0, 0, 0)  # Transparent
        
        # Cache for brightness detection
        self.last_brightness = 255

    def set_parameters(self, model_obj, model_path: Optional[str] = None) -> None:
        """Extract parameter information from Live2D model.
        
        Gets parameter ranges directly from the model objects.
        """
        param_ids = model_obj.GetParamIds()
        
        # Priority mapping of keywords to match (case-insensitive, underscores ignored)
        # Higher priority items should come first
        match_configs = [
            ("BODYANGLEX", "BodyAngleX"),
            ("BODYANGLEY", "BodyAngleY"),
            ("BODYANGLEZ", "BodyAngleZ"),
            ("ANGLEX", "AngleX"),
            ("ANGLEY", "AngleY"),
            ("ANGLEZ", "AngleZ"),
            ("BODYX", "BodyX"),
            ("BODYY", "BodyY"),
            ("BODYZ", "BodyZ"),
            ("ARML", "ArmL"),
            ("ARMR", "ArmR"),
        ]
        
        self.param_info = {}
        self.param_values = {}
        
        # Keep track of which indices we've already matched to avoid duplicates
        matched_indices = set()

        for config_kw, _ in match_configs:
            for i, param_id in enumerate(param_ids):
                if i in matched_indices:
                    continue
                    
                p_norm = param_id.upper().replace("_", "")
                if config_kw in p_norm:
                    # Exclusion for arms
                    if config_kw == "ARML" and "LB" in p_norm: continue
                    if config_kw == "ARMR" and "RB" in p_norm: continue
                    
                    param_obj = model_obj.GetParameter(i)
                    self.param_info[i] = {
                        "name": param_id,
                        "min": float(param_obj.min),
                        "max": float(param_obj.max),
                    }
                    self.param_values[i] = float(param_obj.value)
                    matched_indices.add(i)
                    # For some categories like AngleX, we might only want the first match
                    # but for others we might want more. Let's keep all for now.
                    # break # Uncomment if we only want one match per config_kw


    def update_parameter_values(self, model_obj) -> None:
        """Update current parameter values from model."""
        for param_idx in self.param_info.keys():
            param_obj = model_obj.GetParameter(param_idx)
            self.param_values[param_idx] = param_obj.value

    def detect_background_brightness(self, cv_image: np.ndarray) -> tuple:
        """Detect background brightness and return text color (R, G, B, A)."""
        if cv_image is None or cv_image.shape[0] == 0 or cv_image.shape[1] == 0:
            return (255, 255, 255, 255)  # Default: white text
        
        # Sample a region from the center-left of the image
        h, w = cv_image.shape[:2]
        sample_h = h // 2
        sample_w = w // 4
        sample_region = cv_image[
            max(0, sample_h - 30):min(h, sample_h + 30),
            max(0, sample_w - 30):min(w, sample_w + 30)
        ]
        
        if sample_region.size == 0:
            return (255, 255, 255, 255)
        
        # Convert BGR to RGB if needed
        if len(cv_image.shape) == 3 and cv_image.shape[2] >= 3:
            sample_rgb = cv2.cvtColor(sample_region, cv2.COLOR_BGR2RGB)
        else:
            sample_rgb = sample_region
        
        # Calculate luminance
        if len(sample_rgb.shape) == 3:
            r, g, b = sample_rgb[:, :, 0], sample_rgb[:, :, 1], sample_rgb[:, :, 2]
            luminance = 0.299 * r.astype(float) + 0.587 * g.astype(float) + 0.114 * b.astype(float)
            avg_luminance = np.mean(luminance)
        else:
            avg_luminance = np.mean(sample_rgb)
        
        self.last_brightness = int(avg_luminance)
        
        # Choose text color based on background brightness
        if avg_luminance > 128:
            return (0, 0, 0, 255)  # Black text on light background
        else:
            return (255, 255, 255, 255)  # White text on dark background

    def render_to_image(self, text_color: tuple) -> Image.Image:
        """Render parameter display to PIL Image."""
        # Calculate panel height based on number of parameters
        num_params = len(self.param_info)
        panel_height = self.line_height * num_params + 10
        
        # Create transparent image
        img = Image.new("RGBA", (self.panel_width, panel_height), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)
        
        # Try to load a monospace font, fall back to default
        # Common Windows monospace fonts
        font_paths = [
            "C:/Windows/Fonts/consola.ttf", # Consolas
            "C:/Windows/Fonts/cour.ttf",    # Courier New
            "C:/Windows/Fonts/lucon.ttf",   # Lucida Console
            "consolas.ttf",
            "courier.ttf"
        ]
        
        font = None
        for path in font_paths:
            try:
                font = ImageFont.truetype(path, self.font_size)
                break
            except OSError:
                continue
        
        if font is None:
            font = ImageFont.load_default()
        
        y_offset = 5
        
        # Sort parameters by index for consistent display
        sorted_params = sorted(self.param_info.items())
        
        for param_idx, info in sorted_params:
            param_name = info["name"]
            param_min = info["min"]
            param_max = info["max"]
            param_value = self.param_values.get(param_idx, 0.0)
            
            # Normalize value to 0-1 range
            value_range = param_max - param_min
            if value_range > 0:
                normalized = (param_value - param_min) / value_range
                normalized = max(0, min(1, normalized))  # Clamp to [0, 1]
            else:
                normalized = 0.5
            
            # Build bar graph
            bar_pos = int(normalized * (self.bar_length - 1))
            bar = ["-"] * self.bar_length
            bar[bar_pos] = "*"
            bar_str = "".join(bar)
            
            # Format line
            line = f"{param_name:15} {param_value:7.2f}  {param_min:7.1f}[{bar_str}]{param_max:7.1f}"
            
            # Draw text
            draw.text((5, y_offset), line, fill=text_color, font=font)
            y_offset += self.line_height
        
        return img

    def create_texture(self, width: int, height: int) -> int:
        """Create an OpenGL texture."""
        texture_id = glGenTextures(1)
        glBindTexture(GL_TEXTURE_2D, texture_id)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, None)
        glBindTexture(GL_TEXTURE_2D, 0)
        return texture_id

    def update_texture(self, pil_image: Image.Image) -> None:
        """Update OpenGL texture with PIL image."""
        width, height = pil_image.size
        
        if self.texture_id is None:
            self.texture_id = self.create_texture(width, height)
        
        # Convert PIL image to numpy array (RGBA)
        img_array = np.array(pil_image, dtype=np.uint8)
        
        glBindTexture(GL_TEXTURE_2D, self.texture_id)
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, img_array)
        glBindTexture(GL_TEXTURE_2D, 0)

    def render(self, screen_width: int, screen_height: int) -> None:
        """Render the parameter display as a 2D overlay in top-right corner."""
        if self.texture_id is None or len(self.param_info) == 0:
            return
        
        num_params = len(self.param_info)
        panel_height = self.line_height * num_params + 10
        
        # Save current OpenGL state
        glMatrixMode(GL_PROJECTION)
        glPushMatrix()
        glLoadIdentity()
        glOrtho(0, screen_width, screen_height, 0, -1, 1)
        glMatrixMode(GL_MODELVIEW)
        glPushMatrix()
        glLoadIdentity()
        
        glDisable(GL_DEPTH_TEST)
        glEnable(GL_BLEND)
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
        glEnable(GL_TEXTURE_2D)
        glBindTexture(GL_TEXTURE_2D, self.texture_id)
        
        # Position in top-right corner
        x = screen_width - self.panel_width - self.margin
        y = self.margin
        
        glBegin(GL_QUADS)
        glColor4f(1.0, 1.0, 1.0, 1.0)
        glTexCoord2f(0, 0)
        glVertex2f(x, y)
        glTexCoord2f(1, 0)
        glVertex2f(x + self.panel_width, y)
        glTexCoord2f(1, 1)
        glVertex2f(x + self.panel_width, y + panel_height)
        glTexCoord2f(0, 1)
        glVertex2f(x, y + panel_height)
        glEnd()
        
        glDisable(GL_TEXTURE_2D)
        glDisable(GL_BLEND)
        glEnable(GL_DEPTH_TEST)
        
        # Restore OpenGL state
        glMatrixMode(GL_PROJECTION)
        glPopMatrix()
        glMatrixMode(GL_MODELVIEW)
        glPopMatrix()

    def cleanup(self) -> None:
        """Delete the texture."""
        if self.texture_id is not None:
            glDeleteTextures([self.texture_id])
            self.texture_id = None
