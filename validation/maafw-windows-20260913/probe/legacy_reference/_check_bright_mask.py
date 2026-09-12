def _check_bright_mask(screenImage, template, roi = None, min_brightness = 145, outputMatchResult = False):
        if roi is None or len(roi) == 0:
            search_area = screenImage
            offset_x, offset_y = 0, 0
        elif len(roi) == 1:
            x, y, w, h = roi[0]
            img_h, img_w = screenImage.shape[:2]
            x_start = max(0, x)
            y_start = max(0, y)
            x_end = min(img_w, x + w)
            y_end = min(img_h, y + h)
            if x_start >= x_end or y_start >= y_end:
                logger.error("错误:roi1范围无效.")
                return None, 0
            search_area = screenImage[y_start:y_end, x_start:x_end]
            offset_x, offset_y = x_start, y_start
        else:
            logger.error(_("亮色遮罩匹配仅支持单个ROI."))
            return None, 0

        mask_key = (id(template), min_brightness)
        mask = brightMaskCache.get(mask_key)
        if mask is None:
            gray_template = cv2.cvtColor(template, cv2.COLOR_BGR2GRAY)
            mask = cv2.inRange(gray_template, min_brightness, 255)
            mask = cv2.dilate(mask, np.ones((2, 2), np.uint8), iterations=1)
            brightMaskCache[mask_key] = mask
        if not np.any(mask):
            logger.error(_("亮色遮罩匹配失败: 模板没有足够的亮色像素."))
            return None, 0

        try:
            result = cv2.matchTemplate(search_area, template, cv2.TM_CCORR_NORMED, mask=mask)
            result = np.nan_to_num(result, nan=-1, posinf=-1, neginf=-1)
        except Exception as e:
            logger.error("{a}".format(a=e))
            if isinstance(e, (cv2.error)):
                logger.info(_("cv2异常."))
            return None, 0

        underscore, max_val, underscore, max_loc = cv2.minMaxLoc(result)

        if outputMatchResult:
            debug_image = search_area.copy()
            SaveImage(debug_image, "origin.png")
            cv2.rectangle(debug_image, max_loc, (max_loc[0] + template.shape[1], max_loc[1] + template.shape[0]), (0, 255, 0), 2)
            SaveImage(debug_image, "matched.png")

        pos = [
            offset_x + max_loc[0] + template.shape[1] // 2,
            offset_y + max_loc[1] + template.shape[0] // 2,
        ]
        return pos, max_val
